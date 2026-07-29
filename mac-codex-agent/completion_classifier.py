"""Classify whether a completed Codex turn still needs user action."""

from __future__ import annotations

import dataclasses
import re
from typing import Any, List, Sequence, Tuple


_MEMORY_CITATION_RE = re.compile(
    r"<oai-mem-citation>.*?</oai-mem-citation>", re.DOTALL | re.IGNORECASE
)
_FENCED_CODE_RE = re.compile(r"```.*?```", re.DOTALL)
_INLINE_CODE_RE = re.compile(r"`[^`\n]*`")
_MARKDOWN_LINK_TARGET_RE = re.compile(r"\]\([^)\n]*\)")
_URL_RE = re.compile(r"https?://\S+")
_QUESTION_MARK_CHARS = r"?？؟¿"
_QUESTION_MARK_RE = re.compile(rf"[{_QUESTION_MARK_CHARS}]")
_QUOTED_QUESTION_RE = re.compile(
    r"(?:"
    rf"“[^”\n]*[{_QUESTION_MARK_CHARS}][^”\n]*”|"
    rf"‘[^’\n]*[{_QUESTION_MARK_CHARS}][^’\n]*’|"
    rf"\"[^\"\n]*[{_QUESTION_MARK_CHARS}][^\"\n]*\"|"
    rf"'[^'\n]*[{_QUESTION_MARK_CHARS}][^'\n]*'"
    r")"
)
_NON_PROSE_LINE_RE = re.compile(r"^\s*(?:#{1,6}\s+|>)")
_ANSWER_CUE_RE = re.compile(
    r"(?:原因(?:是|在于)|答案(?:是|为)|"
    r"\bbecause\b|\bthe answer is\b)",
    re.IGNORECASE,
)
_SECOND_PERSON_RE = re.compile(
    r"(?:你|您|\b(?:you|your|yours|yourself|yourselves)\b)",
    re.IGNORECASE,
)
_REQUIRED_ACTION_RE = re.compile(
    r"(?:请(?:确认|选择|提供|回复|重新|复核)|"
    r"确认后(?:继续|我继续|我会继续)|"
    r"等待(?:你的)?(?:回复|确认)|"
    r"需要(?:你|您)(?:确认|选择|提供|回复|操作)|"
    r"需要进一步指令|无法继续|"
    r"\bplease (?:confirm|choose|select|provide|reply|retry|review)\b|"
    r"\bwaiting for your (?:reply|confirmation|choice|input)\b|"
    r"\bneed your (?:confirmation|choice|input|reply)\b|"
    r"\bcannot continue\b)",
    re.IGNORECASE,
)
_COURTESY_RE = re.compile(
    r"(?:如(?:果)?需(?:要)?更多帮助请告诉我|如果你愿意(?:，|,)?请告诉我|"
    r"\blet me know if you (?:would like|want|need) more help\b|"
    r"\bif you(?:'d| would) like(?:,)? let me know\b)",
    re.IGNORECASE,
)
_RESOLUTION_RE = re.compile(
    r"(?:无需(?:你|您)?(?:回复|确认|选择|操作)|"
    r"不需要(?:你|您)?(?:回复|确认|选择|操作)|"
    r"已经按.+?(?:实施|完成)，?不需要再选择|"
    r"问题已经解决，?无需回复|"
    r"\bno (?:reply|confirmation|choice|user action) is required\b|"
    r"\bno action remains\b|"
    r"\bthe (?:question|choice) (?:is|has been) resolved\b)",
    re.IGNORECASE,
)
_DECISION_CUE_RE = re.compile(
    r"(?:选(?:择|用)?|请选择|方案|选项|推荐|"
    r"\b(?:choose|select|pick|reply|respond|option|recommend)\b)",
    re.IGNORECASE,
)
_RECOMMEND_LABEL_RE = re.compile(
    r"(?:推荐|recommend)\s*(?:第\s*)?([A-Z1-9])", re.IGNORECASE
)
_OPTION_LINE_RE = re.compile(
    r"^\s*(?:[-*]\s*)?([A-Z]|[1-9])\s*[.、:：)）]\s*\S+", re.MULTILINE
)
_TEXT_STRENGTH = {
    "text_direct_question": 1,
    "text_choice_handoff": 2,
    "text_required_action": 3,
}


@dataclasses.dataclass(frozen=True)
class CompletionClassification:
    outcome: str
    reason: str


@dataclasses.dataclass(frozen=True)
class _Evidence:
    offset: int
    kind: str
    strength: int


@dataclasses.dataclass(frozen=True)
class _Block:
    text: str
    offset: int


def _strip_non_prose(message: str) -> str:
    text = _MEMORY_CITATION_RE.sub("", message)
    text = _FENCED_CODE_RE.sub("", text)
    text = _INLINE_CODE_RE.sub("", text)
    text = _MARKDOWN_LINK_TARGET_RE.sub("]", text)
    return _URL_RE.sub("", text)


def _blocks(message: str) -> List[_Block]:
    source = _strip_non_prose(message)
    blocks = []
    cursor = 0
    for raw in source.split("\n\n"):
        start = source.find(raw, cursor)
        cursor = start + len(raw) + 2
        text = raw.strip()
        if text:
            leading = len(raw) - len(raw.lstrip())
            blocks.append(_Block(text, start + leading))
    while blocks and blocks[-1].text.startswith("::"):
        blocks.pop()
    return blocks


def _prose(block: _Block) -> str:
    prose = "\n".join(
        " " * len(line) if _NON_PROSE_LINE_RE.match(line) else line
        for line in block.text.splitlines()
    )
    return _QUOTED_QUESTION_RE.sub(lambda match: " " * len(match.group()), prose)


def _overlaps(match: re.Match[str], spans: Sequence[Tuple[int, int]]) -> bool:
    return any(match.start() < end and start < match.end() for start, end in spans)


def _question_evidence(block: _Block, prose: str) -> List[_Evidence]:
    evidence = []
    for question in _QUESTION_MARK_RE.finditer(prose):
        suffix = prose[question.end() :]
        self_answered = (
            _ANSWER_CUE_RE.search(suffix) is not None
            and _SECOND_PERSON_RE.search(prose) is None
            and _REQUIRED_ACTION_RE.search(prose) is None
            and _DECISION_CUE_RE.search(prose) is None
        )
        if not self_answered:
            evidence.append(
                _Evidence(
                    block.offset + question.start(),
                    "text_direct_question",
                    _TEXT_STRENGTH["text_direct_question"],
                )
            )
    return evidence


def _choice_evidence(
    blocks: Sequence[_Block],
    prose_blocks: Sequence[str],
    questions: Sequence[Tuple[int, _Evidence]],
) -> List[_Evidence]:
    evidence = []
    for index, prose in enumerate(prose_blocks):
        labels = []
        for match in _OPTION_LINE_RE.finditer(prose):
            label = match.group(1).upper()
            if label not in labels:
                labels.append(label)
        if len(labels) < 2:
            continue

        nearby = range(max(0, index - 1), min(len(blocks), index + 2))
        question = next((item for item in questions if item[0] in nearby), None)
        cue = next(
            (
                (nearby_index, match)
                for nearby_index in nearby
                for match in _DECISION_CUE_RE.finditer(prose_blocks[nearby_index])
            ),
            None,
        )
        recommendation = next(
            (
                (nearby_index, match)
                for nearby_index in nearby
                for match in _RECOMMEND_LABEL_RE.finditer(prose_blocks[nearby_index])
                if match.group(1).upper() in labels
            ),
            None,
        )
        if question is not None:
            evidence.append(
                _Evidence(
                    question[1].offset,
                    "text_choice_handoff",
                    _TEXT_STRENGTH["text_choice_handoff"],
                )
            )
        elif recommendation is not None:
            nearby_index, match = recommendation
            evidence.append(
                _Evidence(
                    blocks[nearby_index].offset + match.start(),
                    "text_choice_handoff",
                    _TEXT_STRENGTH["text_choice_handoff"],
                )
            )
        elif cue is not None:
            nearby_index, match = cue
            evidence.append(
                _Evidence(
                    blocks[nearby_index].offset + match.start(),
                    "text_choice_handoff",
                    _TEXT_STRENGTH["text_choice_handoff"],
                )
            )
    return evidence


def _text_evidence(blocks: Sequence[_Block]) -> List[_Evidence]:
    prose_blocks = [_prose(block) for block in blocks]
    evidence = []
    questions = []
    for index, (block, prose) in enumerate(zip(blocks, prose_blocks)):
        block_questions = _question_evidence(block, prose)
        evidence.extend(block_questions)
        questions.extend((index, item) for item in block_questions)

        courtesy_spans = [(match.start(), match.end()) for match in _COURTESY_RE.finditer(prose)]
        resolution_spans = [
            (match.start(), match.end()) for match in _RESOLUTION_RE.finditer(prose)
        ]
        evidence.extend(
            _Evidence(block.offset + start, "text_explicitly_resolved", 0)
            for start, _ in resolution_spans
        )
        evidence.extend(
            _Evidence(
                block.offset + match.start(),
                "text_required_action",
                _TEXT_STRENGTH["text_required_action"],
            )
            for match in _REQUIRED_ACTION_RE.finditer(prose)
            if not _overlaps(match, courtesy_spans + resolution_spans)
        )
    evidence.extend(_choice_evidence(blocks, prose_blocks, questions))
    return sorted(evidence, key=lambda item: (item.offset, item.kind))


def classify_completion(
    message: Any,
    *,
    unfinished_plan: bool = False,
    unresolved_input: bool = False,
) -> CompletionClassification:
    if unresolved_input:
        return CompletionClassification("waiting", "structured_input_request")
    if unfinished_plan:
        return CompletionClassification("waiting", "structured_unfinished_plan")
    if not isinstance(message, str):
        return CompletionClassification("completed", "completion_only")

    blocks = _blocks(message)
    if not blocks:
        return CompletionClassification("completed", "completion_only")

    unresolved = []
    saw_resolution = False
    for item in _text_evidence(blocks):
        if item.kind == "text_explicitly_resolved":
            unresolved.clear()
            saw_resolution = True
        else:
            unresolved.append(item)
    if unresolved:
        strongest = max(unresolved, key=lambda item: item.strength)
        return CompletionClassification("waiting", strongest.kind)
    if saw_resolution:
        return CompletionClassification("completed", "text_explicitly_resolved")
    return CompletionClassification("completed", "completion_only")
