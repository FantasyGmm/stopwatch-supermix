"""Classify whether a completed Codex turn still needs user action."""

from __future__ import annotations

import bisect
import dataclasses
import enum
import re
from typing import Any, FrozenSet, List, Sequence, Tuple


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
_SENTENCE_END_RE = re.compile(r"[.!?。！？]")
_QUOTED_QUESTION_RE = re.compile(
    r"(?:"
    rf"“[^”\n]*[{_QUESTION_MARK_CHARS}][^”\n]*”|"
    rf"‘[^’\n]*[{_QUESTION_MARK_CHARS}][^’\n]*’|"
    rf"\"[^\"\n]*[{_QUESTION_MARK_CHARS}][^\"\n]*\"|"
    rf"(?<!\w)'[^'\n]*[{_QUESTION_MARK_CHARS}][^'\n]*'(?!\w)"
    r")"
)
_QUOTED_TEXT_RE = re.compile(
    r"(?:“[^”\n]*”|‘[^’\n]*’|\"[^\"\n]*\"|(?<!\w)'[^'\n]*'(?!\w))"
)
_HISTORICAL_REPORT_RE = re.compile(
    r"(?:\b(?:earlier|previous|previously|prior|historical)\b[^\r\n。！？.!?]{0,60}"
    r"\b(?:asked|said|reported|recorded)\b|"
    r"\b(?:log|record|history)\b[^\r\n。！？.!?]{0,40}"
    r"\b(?:asked|said|reported|recorded)\b|"
    r"(?:之前|先前|早先)[^\r\n。！？!?]{0,40}(?:询问|问道|记录|写道))",
    re.IGNORECASE,
)
_TRAILING_HISTORICAL_REPORT_RE = re.compile(
    r"(?:\b(?:earlier|previous|previously|prior|historical)\b"
    r"[^\r\n。！？.!?]{0,60}\b(?:asked|said|reported|recorded)\b|"
    r"\b(?:log|record|history)\b[^\r\n。！？.!?]{0,40}"
    r"\b(?:asked|said|reported|recorded)\b|"
    r"(?:之前|先前|早先)[^\r\n。！？!?]{0,40}(?:询问|问道|记录|写道))"
    r"\s*[:：]?\s*$",
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
    r"\bcan you (?:confirm|choose|select|provide|reply|retry|review)\b|"
    r"\bwaiting for your (?:reply|confirmation|choice|input)\b|"
    r"\bneed your (?:confirmation|choice|input|reply)\b|"
    r"\bcannot continue\b|\blet me know\b|告诉我)",
    re.IGNORECASE,
)
_COORDINATED_ACTION_RE = re.compile(
    r"\b(?:and|then) (?:confirm|choose|select|provide|reply|retry|review)\b",
    re.IGNORECASE,
)
_COURTESY_CLAUSE_RE = re.compile(
    r"(?:^\s*(?:[-*]\s*)?(?:"
    r"if you(?:'d| would) like\b|"
    r"if you (?:want|need) (?:more|additional|further)\b|"
    r"如果你(?:愿意|想(?:要)?(?:了解)?更多)|"
    r"如(?:果)?需(?:要)?更多)"
    r"|\blet me know if you (?:would like|want|need) "
    r"(?:more|additional|further)\b)",
    re.IGNORECASE,
)
_COURTESY_BREAK_RE = re.compile(
    r"(?:[;；]|,\s*(?:but|however)\b|，?(?:但|但是|不过))",
    re.IGNORECASE,
)
_CLAUSE_RE = re.compile(r"[^\r\n。！？.!?]+(?:[。！？.!?]+|$)")
_RESOLUTION_RE = re.compile(
    r"(?P<reply>"
    r"(?:无需|不需要)(?:你|您)?回复|"
    r"问题已经解决，?无需回复|"
    r"\bno reply is required\b|"
    r"\bthe (?:(?:earlier|previous|prior) )?question (?:is|has been) resolved\b)"
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
_INVALID_RESOLUTION_CONTEXT_RE = re.compile(
    r"(?:\bnot (?:true|the case) that\b|"
    r"\b(?:docs?|documentation|examples?) (?:say|says|state|states)\b|"
    r"\baccording to (?:the )?(?:docs?|documentation|examples?)\b|"
    r"\b(?:after|once|when|if|until|will)\b|"
    r"(?:并非|不是|文档(?:说|写道|指出)|之后|以后|如果|待))",
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
    r"从以下[^\r\n。！？!?]{0,30}选择一项|选择(?:一项|一个|其一)|"
    r"(?:你|您)[^\r\n。！？!?]{0,40}(?:选择|"
    r"选(?=[ \t]*(?:[A-Z1-9]|哪|一|这|那|方案|选项))|"
    r"希望(?:如何|怎么|哪(?:个|种)?))|"
    r"\b(?:choose|select|pick)\s+(?:one|an? option|one of)\b|"
    r"(?:^|(?<=[。！？.!?]))[ \t]*(?:choose|select|pick)\b|"
    r"\b(?:reply|respond)\s+with\b|"
    r"\b(?:which|what|how)\b[^\r\n.!?]{0,50}\b(?:you|your)\b)",
    re.IGNORECASE | re.MULTILINE,
)
_RECOMMEND_LABEL_RE = re.compile(
    r"(?:推荐|recommend\b)\s*(?:(?:option|方案)\s*)?(?:第\s*)?([A-Z1-9])",
    re.IGNORECASE,
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
    EXECUTION = "execution"


_COMMUNICATIVE_SCOPES = frozenset(
    {
        _EvidenceScope.REPLY,
        _EvidenceScope.CONFIRMATION,
        _EvidenceScope.CHOICE,
    }
)
_ALL_SCOPES = frozenset(set(_EvidenceScope))


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
    scopes: FrozenSet[_EvidenceScope]


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


def _prose(block: _Block, *, inherited_historical_report: bool = False) -> str:
    prose = "\n".join(
        " " * len(line) if _NON_PROSE_LINE_RE.match(line) else line
        for line in block.text.splitlines()
    )
    masked = list(prose)
    sentence_end_offsets = [match.start() for match in _SENTENCE_END_RE.finditer(prose)]
    for quote in _QUOTED_QUESTION_RE.finditer(prose):
        sentence_index = bisect.bisect_left(sentence_end_offsets, quote.start()) - 1
        sentence_start = sentence_end_offsets[sentence_index] if sentence_index >= 0 else -1
        context = prose[sentence_start + 1 : quote.start()]
        if inherited_historical_report or _HISTORICAL_REPORT_RE.search(context) is not None:
            masked[quote.start() : quote.end()] = " " * len(quote.group())
    return "".join(masked)


def _courtesy_spans(prose: str) -> List[Tuple[int, int]]:
    spans = []
    for clause in _CLAUSE_RE.finditer(prose):
        if _COURTESY_CLAUSE_RE.search(clause.group()) is None:
            continue
        boundary = _COURTESY_BREAK_RE.search(clause.group())
        end = clause.start() + boundary.start() if boundary is not None else clause.end()
        spans.append((clause.start(), end))
    return spans


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


def _action_scopes(match: re.Match[str], clause: str) -> FrozenSet[_EvidenceScope]:
    action = match.group().casefold()
    if re.search(
        r"(?:重新|复核|操作|\b(?:retry|review)\b)", action, re.IGNORECASE
    ):
        return frozenset({_EvidenceScope.EXECUTION})
    if re.search(r"(?:选择|\bchoose\b|\bselect\b)", action, re.IGNORECASE):
        return frozenset({_EvidenceScope.CHOICE})
    if re.search(r"(?:确认|\bconfirm\b)", action, re.IGNORECASE):
        return frozenset({_EvidenceScope.CONFIRMATION})
    communicative_choice = re.search(
        r"\b[A-Z1-9]\b[^\r\n。！？.!?]{0,20}\b(?:or|and)\b"
        r"[^\r\n。！？.!?]{0,20}\b[A-Z1-9]\b|"
        r"[A-Z1-9][、，,或][A-Z1-9]",
        clause,
        re.IGNORECASE,
    )
    if (
        re.search(r"(?:回复|\breply\b|\brespond\b)", action, re.IGNORECASE)
        and communicative_choice
    ):
        return frozenset({_EvidenceScope.CHOICE})
    if re.search(
        r"(?:提供|回复|告诉我|需要进一步指令|"
        r"\b(?:provide|reply|let me know|waiting for|need your)\b)",
        action,
        re.IGNORECASE,
    ):
        return frozenset({_EvidenceScope.REPLY})
    return frozenset({_EvidenceScope.EXECUTION})


def _resolution_scopes(match: re.Match[str]) -> FrozenSet[_EvidenceScope]:
    if match.lastgroup == "reply":
        return _COMMUNICATIVE_SCOPES
    if match.lastgroup == "confirmation":
        return frozenset({_EvidenceScope.CONFIRMATION})
    if match.lastgroup == "choice":
        return frozenset({_EvidenceScope.CHOICE})
    return _ALL_SCOPES


def _valid_resolution_matches(prose: str) -> List[re.Match[str]]:
    clauses = list(_CLAUSE_RE.finditer(prose))
    quoted_spans = [(match.start(), match.end()) for match in _QUOTED_TEXT_RE.finditer(prose)]
    matches = []
    clause_index = 0
    quote_index = 0
    for match in _RESOLUTION_RE.finditer(prose):
        while quote_index < len(quoted_spans) and quoted_spans[quote_index][1] <= match.start():
            quote_index += 1
        if quote_index < len(quoted_spans) and quoted_spans[quote_index][0] < match.end():
            continue
        while (
            clause_index + 1 < len(clauses)
            and match.start() >= clauses[clause_index].end()
        ):
            clause_index += 1
        if clauses:
            clause = clauses[clause_index]
            context = clause.group()
        else:
            context = prose
        if _INVALID_RESOLUTION_CONTEXT_RE.search(context) is None:
            matches.append(match)
    return matches


def _question_evidence(
    block: _Block,
    prose: str,
    actions: Sequence[_Evidence],
) -> List[_Evidence]:
    evidence = []
    questions = list(_QUESTION_MARK_RE.finditer(prose))
    clauses = list(_CLAUSE_RE.finditer(prose))
    action_offsets = [action.offset for action in actions]
    clause_index = 0
    for index, question in enumerate(questions):
        while (
            clause_index + 1 < len(clauses)
            and question.start() >= clauses[clause_index].end()
        ):
            clause_index += 1
        clause = clauses[clause_index].group() if clauses else prose
        suffix_end = (
            questions[index + 1].start()
            if index + 1 < len(questions)
            else len(prose)
        )
        suffix = prose[question.end() : suffix_end]
        answer_cue = _ANSWER_CUE_RE.search(suffix)
        context_start = clauses[clause_index].start() if clauses else 0
        context_end = suffix_end
        if answer_cue is not None and clauses:
            answer_offset = question.end() + answer_cue.start()
            answer_clause_index = clause_index
            while (
                answer_clause_index + 1 < len(clauses)
                and answer_offset >= clauses[answer_clause_index].end()
            ):
                answer_clause_index += 1
            context_end = clauses[answer_clause_index].end()
        answer_context = prose[context_start:context_end]
        decision_context = prose[context_start:suffix_end]
        action_index = bisect.bisect_left(action_offsets, block.offset + context_start)
        has_context_action = (
            action_index < len(action_offsets)
            and action_offsets[action_index] < block.offset + context_end
        )
        self_answered = (
            answer_cue is not None
            and _SECOND_PERSON_RE.search(answer_context) is None
            and not has_context_action
            and _SELF_ANSWER_DECISION_RE.search(decision_context) is None
        )
        if not self_answered:
            evidence.append(
                _Evidence(
                    block.offset + question.start(),
                    _EvidenceKind.DIRECT_QUESTION,
                    _TEXT_STRENGTH[_EvidenceKind.DIRECT_QUESTION],
                    frozenset({_question_scope(clause)}),
                )
            )
    return evidence


def _choice_evidence(
    blocks: Sequence[_Block],
    prose_blocks: Sequence[str],
    resolutions: Sequence[_Evidence],
    questions_by_block: Sequence[Sequence[_Evidence]],
) -> List[_Evidence]:
    evidence = []
    choice_resolution_offsets = sorted(
        resolution.offset
        for resolution in resolutions
        if _EvidenceScope.CHOICE in resolution.scopes
    )
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
        option_line_spans = []
        for match in option_matches:
            line_end = prose.find("\n", match.start())
            option_line_spans.append(
                (match.start(), line_end if line_end >= 0 else len(prose))
            )
        option_line_starts = [start for start, _ in option_line_spans]

        def in_option_body(offset: int) -> bool:
            span_index = bisect.bisect_right(option_line_starts, offset) - 1
            return span_index >= 0 and offset < option_line_spans[span_index][1]

        anchors = []
        for question_block in (index - 1, index):
            if 0 <= question_block < len(questions_by_block):
                anchors.extend(
                    question.offset
                    for question in questions_by_block[question_block]
                    if question.offset < group_offset
                )
        nearby = range(max(0, index - 1), min(len(blocks), index + 2))
        for nearby_index in nearby:
            for match in _CHOICE_HANDOFF_CUE_RE.finditer(prose_blocks[nearby_index]):
                inside_option = nearby_index == index and in_option_body(match.start())
                if not inside_option:
                    anchors.append(blocks[nearby_index].offset + match.start())
            for match in _RECOMMEND_LABEL_RE.finditer(prose_blocks[nearby_index]):
                inside_option = nearby_index == index and in_option_body(match.start())
                if not inside_option and match.group(1).upper() in labels:
                    anchors.append(blocks[nearby_index].offset + match.start())
        for anchor in sorted(anchors):
            lower, upper = sorted((group_offset, anchor))
            resolution_index = bisect.bisect_right(choice_resolution_offsets, lower)
            blocked = (
                resolution_index < len(choice_resolution_offsets)
                and choice_resolution_offsets[resolution_index] < upper
            )
            if not blocked:
                evidence.append(
                    _Evidence(
                        anchor,
                        _EvidenceKind.CHOICE_HANDOFF,
                        _TEXT_STRENGTH[_EvidenceKind.CHOICE_HANDOFF],
                        frozenset({_EvidenceScope.CHOICE}),
                    )
                )
                break
    return evidence


def _required_action_evidence(
    block: _Block,
    prose: str,
    excluded_spans: Sequence[Tuple[int, int]],
) -> List[_Evidence]:
    clauses = list(_CLAUSE_RE.finditer(prose))
    primary_matches = list(_REQUIRED_ACTION_RE.finditer(prose))
    primary_starts = [match.start() for match in primary_matches]
    action_matches = list(primary_matches)
    clause_index = 0
    for match in _COORDINATED_ACTION_RE.finditer(prose):
        while (
            clause_index + 1 < len(clauses)
            and match.start() >= clauses[clause_index].end()
        ):
            clause_index += 1
        clause = clauses[clause_index] if clauses else None
        primary_index = bisect.bisect_left(primary_starts, match.start()) - 1
        if (
            clause is not None
            and primary_index >= 0
            and primary_starts[primary_index] >= clause.start()
        ):
            action_matches.append(match)

    evidence = []
    clause_index = 0
    excluded = sorted(excluded_spans)
    excluded_index = 0
    for match in sorted(action_matches, key=lambda item: item.start()):
        while excluded_index < len(excluded) and excluded[excluded_index][1] <= match.start():
            excluded_index += 1
        if excluded_index < len(excluded) and excluded[excluded_index][0] < match.end():
            continue
        while (
            clause_index + 1 < len(clauses)
            and match.start() >= clauses[clause_index].end()
        ):
            clause_index += 1
        clause = clauses[clause_index].group() if clauses else prose
        evidence.append(
            _Evidence(
                block.offset + match.start(),
                _EvidenceKind.REQUIRED_ACTION,
                _TEXT_STRENGTH[_EvidenceKind.REQUIRED_ACTION],
                _action_scopes(match, clause),
            )
        )
    return evidence


def _text_evidence(blocks: Sequence[_Block]) -> List[_Evidence]:
    prose_blocks = []
    for index, block in enumerate(blocks):
        inherited_report = (
            index > 0
            and _TRAILING_HISTORICAL_REPORT_RE.search(blocks[index - 1].text) is not None
            and _QUOTED_QUESTION_RE.match(block.text.lstrip()) is not None
        )
        prose_blocks.append(_prose(block, inherited_historical_report=inherited_report))
    evidence = []
    resolutions = []
    questions_by_block = [[] for _ in blocks]
    for block_index, (block, prose) in enumerate(zip(blocks, prose_blocks)):
        courtesy_spans = _courtesy_spans(prose)
        resolution_matches = _valid_resolution_matches(prose)
        resolution_spans = [(match.start(), match.end()) for match in resolution_matches]
        block_resolutions = [
            _Evidence(
                block.offset + match.start(),
                _EvidenceKind.RESOLUTION,
                0,
                _resolution_scopes(match),
            )
            for match in resolution_matches
        ]
        evidence.extend(block_resolutions)
        resolutions.extend(block_resolutions)
        actions = _required_action_evidence(
            block,
            prose,
            courtesy_spans + resolution_spans,
        )
        evidence.extend(actions)
        block_questions = _question_evidence(block, prose, actions)
        evidence.extend(block_questions)
        questions_by_block[block_index].extend(block_questions)
    evidence.extend(
        _choice_evidence(blocks, prose_blocks, resolutions, questions_by_block)
    )
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
                if pending.scopes.isdisjoint(item.scopes)
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
