"""Classify whether a completed Codex turn still needs user action."""

from __future__ import annotations

import dataclasses
import enum
import re
from typing import Any, List, Sequence, Tuple


_MEMORY_CITATION_RE = re.compile(
    r"<oai-mem-citation>.*?</oai-mem-citation>", re.DOTALL | re.IGNORECASE
)
_FENCED_CODE_RE = re.compile(r"```.*?```", re.DOTALL)
_INLINE_CODE_RE = re.compile(r"`[^`\n]*`")
_MARKDOWN_LINK_TARGET_RE = re.compile(r"\]\([^)\n]*\)")
_URL_RE = re.compile(r"https?://\S+")
_BLOCK_BREAK_RE = re.compile(r"\r?\n[ \t]*\r?\n")
_QUESTION_MARK_CHARS = r"?？؟¿"
_QUESTION_MARK_RE = re.compile(rf"[{_QUESTION_MARK_CHARS}]")
_QUOTED_QUESTION_RE = re.compile(
    r"(?:"
    rf"“[^”\n]*[{_QUESTION_MARK_CHARS}][^”\n]*”|"
    rf"‘[^’\n]*[{_QUESTION_MARK_CHARS}][^’\n]*’|"
    rf"\"[^\"\n]*[{_QUESTION_MARK_CHARS}][^\"\n]*\"|"
    rf"(?<!\w)'[^'\n]*[{_QUESTION_MARK_CHARS}][^'\n]*'(?!\w)"
    r")"
)
_HISTORICAL_REPORT_RE = re.compile(
    r"(?:\b(?:earlier|previous|previously|prior|historical)\b[^\r\n。！？.!?]{0,60}"
    r"\b(?:asked|said|reported|recorded)\b|"
    r"(?:之前|先前|早先)[^\r\n。！？!?]{0,40}(?:询问|问道|记录|写道))",
    re.IGNORECASE,
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
    r"\bcannot continue\b|\blet me know\b|告诉我)",
    re.IGNORECASE,
)
_COURTESY_CLAUSE_RE = re.compile(
    r"(?:^\s*(?:"
    r"if you(?:'d| would) like\b|"
    r"if you (?:want|need) (?:more|additional|further)\b|"
    r"如果你(?:愿意|想(?:要)?(?:了解)?更多)|"
    r"如(?:果)?需(?:要)?更多)"
    r"|\blet me know if you (?:would like|want|need) "
    r"(?:more|additional|further)\b)",
    re.IGNORECASE,
)
_CLAUSE_RE = re.compile(r"[^\r\n。！？.!?]+(?:[。！？.!?]+|$)")
_RESOLUTION_RE = re.compile(
    r"(?P<reply>"
    r"(?:无需|不需要)(?:你|您)?回复|"
    r"问题已经解决，?无需回复|"
    r"\bno reply is required\b|"
    r"\bthe question (?:is|has been) resolved\b)"
    r"|(?P<confirmation>"
    r"(?:无需|不需要)(?:你|您)?确认|"
    r"\bno confirmation is required\b)"
    r"|(?P<choice>"
    r"(?:无需|不需要)(?:你|您)?(?:选择|再选择)|"
    r"已经按[^\r\n。！？!?]{0,200}(?:实施|完成)[，,]?不需要再选择|"
    r"\bno (?:choice|selection) is required\b|"
    r"\bthe choice (?:is|has been) resolved\b|"
    r"\bthe previously offered choice has already been applied\b)"
    r"|(?P<all>"
    r"(?:无需|不需要)(?:你|您)?操作|"
    r"\bno (?:user )?action is required\b|"
    r"\bno action remains\b)",
    re.IGNORECASE,
)
_SELF_ANSWER_DECISION_RE = re.compile(
    r"(?:(?:方案|选项)[^\r\n。！？!?]{0,24}"
    r"(?:稍后(?:再)?定|稍后决定|待定|尚未(?:决定|选择)|再(?:定|选择|决定))|"
    r"\b(?:choice|decision|option)[^\r\n.!?]{0,24}"
    r"(?:remains|is pending|will be decided later)\b)",
    re.IGNORECASE,
)
_CHOICE_HANDOFF_CUE_RE = re.compile(
    r"(?:请(?:选择|选)|"
    r"(?:你|您)[^\r\n。！？!?]{0,40}(?:选择|"
    r"选(?=[ \t]*(?:[A-Z1-9]|哪|一|这|那|方案|选项))|"
    r"希望(?:如何|怎么|哪(?:个|种)?))|"
    r"(?:^|(?<=[。！？.!?]))[ \t]*(?:choose|select|pick)\b|"
    r"\b(?:reply|respond)\s+with\b|"
    r"\b(?:which|what|how)\b[^\r\n.!?]{0,50}\b(?:you|your)\b)",
    re.IGNORECASE | re.MULTILINE,
)
_RECOMMEND_LABEL_RE = re.compile(
    r"(?:推荐|recommend\b)\s*(?:第\s*)?([A-Z1-9])", re.IGNORECASE
)
_OPTION_LINE_RE = re.compile(
    r"^\s*(?:[-*]\s*)?([A-Z]|[1-9])\s*[.、:：)）]\s*\S+", re.MULTILINE
)


class _EvidenceKind(enum.Enum):
    DIRECT_QUESTION = "text_direct_question"
    CHOICE_HANDOFF = "text_choice_handoff"
    REQUIRED_ACTION = "text_required_action"
    RESOLUTION = "text_explicitly_resolved"


class _EvidenceScope(enum.Enum):
    REPLY = "reply"
    CONFIRMATION = "confirmation"
    CHOICE = "choice"
    ALL = "all"


_TEXT_STRENGTH = {
    _EvidenceKind.DIRECT_QUESTION: 1,
    _EvidenceKind.CHOICE_HANDOFF: 2,
    _EvidenceKind.REQUIRED_ACTION: 3,
}


@dataclasses.dataclass(frozen=True)
class CompletionClassification:
    outcome: str
    reason: str


@dataclasses.dataclass(frozen=True)
class _Evidence:
    offset: int
    kind: _EvidenceKind
    strength: int
    scope: _EvidenceScope


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
    start = 0
    boundaries = list(_BLOCK_BREAK_RE.finditer(source))
    for boundary in boundaries + [None]:
        end = boundary.start() if boundary is not None else len(source)
        raw = source[start:end]
        text = raw.strip()
        if text:
            leading = len(raw) - len(raw.lstrip())
            blocks.append(_Block(text, start + leading))
        if boundary is not None:
            start = boundary.end()
    while blocks and blocks[-1].text.startswith("::"):
        blocks.pop()
    return blocks


def _prose(block: _Block) -> str:
    prose = "\n".join(
        " " * len(line) if _NON_PROSE_LINE_RE.match(line) else line
        for line in block.text.splitlines()
    )
    masked = list(prose)
    for quote in _QUOTED_QUESTION_RE.finditer(prose):
        sentence_start = max(
            (prose.rfind(mark, 0, quote.start()) for mark in ".!?。！？"),
            default=-1,
        )
        context = prose[sentence_start + 1 : quote.start()]
        if _HISTORICAL_REPORT_RE.search(context) is not None:
            masked[quote.start() : quote.end()] = " " * len(quote.group())
    return "".join(masked)


def _overlaps(match: re.Match[str], spans: Sequence[Tuple[int, int]]) -> bool:
    return any(match.start() < end and start < match.end() for start, end in spans)


def _courtesy_spans(prose: str) -> List[Tuple[int, int]]:
    return [
        (clause.start(), clause.end())
        for clause in _CLAUSE_RE.finditer(prose)
        if _COURTESY_CLAUSE_RE.search(clause.group()) is not None
    ]


def _question_scope(prose: str) -> _EvidenceScope:
    if re.search(
        r"(?:还是|哪(?:个|种)|如何|怎么|\bwhich\b|\bwhat option\b)",
        prose,
        re.IGNORECASE,
    ):
        return _EvidenceScope.CHOICE
    if re.search(r"(?:确认|\bconfirm)", prose, re.IGNORECASE):
        return _EvidenceScope.CONFIRMATION
    return _EvidenceScope.REPLY


def _action_scope(match: re.Match[str]) -> _EvidenceScope:
    action = match.group().casefold()
    if re.search(r"(?:选择|\bchoose\b|\bselect\b)", action, re.IGNORECASE):
        return _EvidenceScope.CHOICE
    if re.search(r"(?:确认|\bconfirm\b)", action, re.IGNORECASE):
        return _EvidenceScope.CONFIRMATION
    return _EvidenceScope.REPLY


def _resolution_scope(match: re.Match[str]) -> _EvidenceScope:
    return _EvidenceScope(match.lastgroup)


def _question_evidence(block: _Block, prose: str) -> List[_Evidence]:
    evidence = []
    questions = list(_QUESTION_MARK_RE.finditer(prose))
    for index, question in enumerate(questions):
        suffix_end = questions[index + 1].start() if index + 1 < len(questions) else len(prose)
        suffix = prose[question.end() : suffix_end]
        self_answered = (
            _ANSWER_CUE_RE.search(suffix) is not None
            and _SECOND_PERSON_RE.search(prose) is None
            and _REQUIRED_ACTION_RE.search(prose) is None
            and _SELF_ANSWER_DECISION_RE.search(prose) is None
        )
        if not self_answered:
            evidence.append(
                _Evidence(
                    block.offset + question.start(),
                    _EvidenceKind.DIRECT_QUESTION,
                    _TEXT_STRENGTH[_EvidenceKind.DIRECT_QUESTION],
                    _question_scope(prose),
                )
            )
    return evidence


def _choice_evidence(
    blocks: Sequence[_Block],
    prose_blocks: Sequence[str],
    resolutions: Sequence[_Evidence],
) -> List[_Evidence]:
    evidence = []
    for index, prose in enumerate(prose_blocks):
        option_matches = list(_OPTION_LINE_RE.finditer(prose))
        labels = []
        for match in option_matches:
            label = match.group(1).upper()
            if label not in labels:
                labels.append(label)
        if len(labels) < 2:
            continue

        group_offset = blocks[index].offset + option_matches[0].start()
        option_line_spans = [
            (
                match.start(),
                prose.find("\n", match.start())
                if "\n" in prose[match.start() :]
                else len(prose),
            )
            for match in option_matches
        ]
        anchors = []
        nearby = range(max(0, index - 1), min(len(blocks), index + 2))
        for nearby_index in nearby:
            for match in _CHOICE_HANDOFF_CUE_RE.finditer(prose_blocks[nearby_index]):
                in_option_body = nearby_index == index and any(
                    start <= match.start() < end for start, end in option_line_spans
                )
                if not in_option_body:
                    anchors.append(blocks[nearby_index].offset + match.start())
            for match in _RECOMMEND_LABEL_RE.finditer(prose_blocks[nearby_index]):
                in_option_body = nearby_index == index and any(
                    start <= match.start() < end for start, end in option_line_spans
                )
                if not in_option_body and match.group(1).upper() in labels:
                    anchors.append(blocks[nearby_index].offset + match.start())
        for anchor in sorted(anchors):
            lower, upper = sorted((group_offset, anchor))
            blocked = any(
                lower < resolution.offset < upper
                and resolution.scope in (_EvidenceScope.CHOICE, _EvidenceScope.ALL)
                for resolution in resolutions
            )
            if not blocked:
                evidence.append(
                    _Evidence(
                        anchor,
                        _EvidenceKind.CHOICE_HANDOFF,
                        _TEXT_STRENGTH[_EvidenceKind.CHOICE_HANDOFF],
                        _EvidenceScope.CHOICE,
                    )
                )
                break
    return evidence


def _text_evidence(blocks: Sequence[_Block]) -> List[_Evidence]:
    prose_blocks = [_prose(block) for block in blocks]
    evidence = []
    resolutions = []
    for block, prose in zip(blocks, prose_blocks):
        block_questions = _question_evidence(block, prose)
        evidence.extend(block_questions)

        courtesy_spans = _courtesy_spans(prose)
        resolution_matches = list(_RESOLUTION_RE.finditer(prose))
        resolution_spans = [(match.start(), match.end()) for match in resolution_matches]
        block_resolutions = [
            _Evidence(
                block.offset + match.start(),
                _EvidenceKind.RESOLUTION,
                0,
                _resolution_scope(match),
            )
            for match in resolution_matches
        ]
        evidence.extend(block_resolutions)
        resolutions.extend(block_resolutions)
        evidence.extend(
            _Evidence(
                block.offset + match.start(),
                _EvidenceKind.REQUIRED_ACTION,
                _TEXT_STRENGTH[_EvidenceKind.REQUIRED_ACTION],
                _action_scope(match),
            )
            for match in _REQUIRED_ACTION_RE.finditer(prose)
            if not _overlaps(match, courtesy_spans + resolution_spans)
        )
    evidence.extend(_choice_evidence(blocks, prose_blocks, resolutions))
    return sorted(evidence, key=lambda item: (item.offset, item.kind.value))


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
        if item.kind is _EvidenceKind.RESOLUTION:
            unresolved = [
                pending
                for pending in unresolved
                if item.scope is not _EvidenceScope.ALL and pending.scope is not item.scope
            ]
            saw_resolution = True
        else:
            unresolved.append(item)
    if unresolved:
        strongest = max(unresolved, key=lambda item: item.strength)
        return CompletionClassification("waiting", strongest.kind.value)
    if saw_resolution:
        return CompletionClassification("completed", "text_explicitly_resolved")
    return CompletionClassification("completed", "completion_only")
