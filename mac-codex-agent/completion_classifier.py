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
_SENTENCE_END_RE = re.compile(r"[.!?。！？؟]")
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
    r"\bplease[ \t]+(?:confirm|choose|select|provide|reply|retry|review)\b|"
    r"\bcan you (?:confirm|choose|select|provide|reply|retry|review)\b|"
    r"\bwaiting for your (?:reply|confirmation|choice|input)\b|"
    r"\bneed your (?:confirmation|choice|input|reply)\b|"
    r"\bcannot continue\b|\blet me know\b|告诉我)",
    re.IGNORECASE,
)
_IMPERATIVE_ACTION_RE = re.compile(
    r"\b(?:confirm|choose|select|provide|reply(?:\s+with)?|retry|review)\b|"
    r"(?:确认|选择|提供|回复|重新|重试|复核)",
    re.IGNORECASE,
)
_IMPERATIVE_BOUNDARY_RE = re.compile(r"[;；:：]|[ \t]+[—–-][ \t]+")
_COORDINATED_ACTION_RE = re.compile(
    r"\b(?:and|then)\s+(?:also\s+)?"
    r"(?:confirm|choose|select|provide|reply|retry|review)\b|"
    r"(?:并且|并|然后)(?:[^\r\n。！？.!?؟]{0,20}?再)?(?:请)?"
    r"(?:确认|选择|提供|回复|重新|重试|复核)",
    re.IGNORECASE,
)
_COMMUNICATIVE_CHOICE_RE = re.compile(
    r"\b[A-Z1-9]\b[^\r\n。！？.!?؟]{0,20}\b(?:or|and)\b"
    r"[^\r\n。！？.!?؟]{0,20}\b[A-Z1-9]\b|"
    r"[A-Z1-9]\s*[、，,或]\s*[A-Z1-9]",
    re.IGNORECASE,
)
_COURTESY_CLAUSE_RE = re.compile(
    r"(?:^\s*(?:[-*]\s*)?(?:"
    r"if you(?:'d| would) like\b|"
    r"if you (?:want|need) (?:more|additional|further)\b|"
    r"如果你(?:愿意|想(?:要)?(?:了解)?更多)|"
    r"如(?:果)?需(?:要)?更多)"
    r"|\blet me know if you(?:'d| would) like "
    r"(?:more|additional|further)\b|"
    r"\blet me know if you (?:want|need) "
    r"(?:more|additional|further)\b)",
    re.IGNORECASE,
)
_COURTESY_BREAK_RE = re.compile(
    r"(?:[;；]|,\s*(?:but|however)\b|"
    r",\s*(?:and|then)\s+(?=(?:please\s+)?"
    r"(?:confirm|choose|select|provide|reply|retry|review)\b)|"
    r"，?(?:但|但是|不过)|"
    r"，?\s*(?:并且|并|然后)(?=(?:请)?"
    r"(?:确认|选择|提供|回复|重新|重试|复核)))",
    re.IGNORECASE,
)
_CLAUSE_RE = re.compile(r"[^\r\n。！？.!?؟]+(?:[。！？.!?؟]+|$)")
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
_INVALID_RESOLUTION_PREFIX_RE = re.compile(
    r"(?:\b(?:is not|isn't|was not|wasn't|not) (?:true|the case) that\b|"
    r"\b(?:do not|don't) assume\b|"
    r"\b(?:docs?|documentation|examples?|logs?) "
    r"(?:say|says|state|states|said)\b|"
    r"\b(?:according to|per) (?:the )?"
    r"(?:docs?|documentation|examples?|logs?)\b|"
    r"\b(?:after|once|when|if|until|then)\b|"
    r"(?:并非|不是|文档(?:说|写道|指出)|根据文档|之后|以后|如果|待))",
    re.IGNORECASE,
)
_INVALID_RESOLUTION_SUFFIX_RE = re.compile(
    r"(?:\b(?:after|once|when|if|until)\b|"
    r"\baccording to (?:the )?(?:docs?|documentation|examples?)\b|"
    r"(?:之后|以后|如果|待))",
    re.IGNORECASE,
)
_ASSISTANT_CONTINUATION_SUFFIX_RE = re.compile(
    r"^\s*[，,;；]?\s*(?:(?:afterward|afterwards|later|then)\s+)?"
    r"I(?:'ll|\s+will)\b|"
    r"^\s*[，,;；]?\s*(?:之后|以后|然后)?我(?:会|将)",
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
    r"^[ \t]*(?:[-*][ \t]*)?([A-Z]|[1-9])\s*[.、:：)）][ \t]*\S+",
    re.MULTILINE,
)
_OPTION_HEADING_RE = re.compile(
    r"^(?:(?:Deployment|Available) options:|Options:|可用方案：|选项：|方案：)\s*$",
    re.IGNORECASE | re.MULTILINE,
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
    referent: str | None = None


@dataclasses.dataclass(frozen=True)
class _Block:
    text: str
    offset: int


@dataclasses.dataclass(frozen=True)
class _Clause:
    text: str
    start: int
    end: int
    question_scope: _EvidenceScope
    communicative_choice: bool


@dataclasses.dataclass(frozen=True)
class _OptionIndex:
    matches: Tuple[re.Match[str], ...]
    line_spans: Tuple[Tuple[int, int], ...]
    body_spans: Tuple[Tuple[int, int], ...]


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
        prior_sentence_start = (
            sentence_end_offsets[sentence_index - 1] + 1 if sentence_index > 0 else 0
        )
        prior_sentence = (
            prose[prior_sentence_start : sentence_start + 1]
            if sentence_index >= 0
            else ""
        )
        inherited = (
            inherited_historical_report
            and prose[: quote.start()].strip() == ""
        )
        same_sentence = _HISTORICAL_REPORT_RE.search(context) is not None
        previous_sentence = (
            context.strip() == ""
            and _TRAILING_HISTORICAL_REPORT_RE.search(
                prior_sentence.rstrip(".。!?！？؟ ")
            )
            is not None
        )
        if inherited or same_sentence or previous_sentence:
            masked[quote.start() : quote.end()] = " " * len(quote.group())
            if inherited:
                inherited_historical_report = False
    return "".join(masked)


def _courtesy_spans(clauses: Sequence[_Clause]) -> List[Tuple[int, int]]:
    spans = []
    for clause in clauses:
        boundaries = list(_COURTESY_BREAK_RE.finditer(clause.text))
        segments = []
        segment_start = 0
        for boundary in boundaries:
            segments.append((segment_start, boundary.start()))
            segment_start = boundary.end()
        segments.append((segment_start, len(clause.text)))
        for start, end in segments:
            if _COURTESY_CLAUSE_RE.search(clause.text[start:end]) is not None:
                spans.append((clause.start + start, clause.start + end))
    return spans


def _question_scope(prose: str) -> _EvidenceScope:
    if _COMMUNICATIVE_CHOICE_RE.search(prose) is not None or re.search(
        r"(?:还是|哪(?:个|种)|如何|怎么|\bwhich\b|\bwhat option\b)",
        prose,
        re.IGNORECASE,
    ):
        return _EvidenceScope.CHOICE
    if re.search(r"(?:确认|\bconfirm)", prose, re.IGNORECASE):
        return _EvidenceScope.CONFIRMATION
    if re.search(
        r"(?:\b(?:can|may|should|could) I\b|(?:可以|继续)吗)",
        prose,
        re.IGNORECASE,
    ):
        return _EvidenceScope.CONFIRMATION
    return _EvidenceScope.REPLY


def _clauses(prose: str) -> List[_Clause]:
    return [
        _Clause(
            text=match.group(),
            start=match.start(),
            end=match.end(),
            question_scope=_question_scope(match.group()),
            communicative_choice=_COMMUNICATIVE_CHOICE_RE.search(match.group())
            is not None,
        )
        for match in _CLAUSE_RE.finditer(prose)
    ]


def _normalize_referent(text: str) -> str | None:
    referent = re.sub(r"\s+", " ", text).strip().casefold()
    referent = re.sub(r"^(?:the|a|an)\s+", "", referent)
    referent = re.sub(r"^(?:to|into|onto|on|in|with)\s+", "", referent)
    return referent or None


_REFERENT_BREAK_RE = re.compile(
    r"[，,;；。.!?！？؟]|"
    r"\b(?:because|since|although|though|while|unless)\b|"
    r"\b(?:and|then)\s+(?:also\s+)?(?="
    r"(?:confirm|choose|select|provide|reply|retry|review)\b)|"
    r"\bplease\s+(?=(?:confirm|choose|select|provide|reply|retry|review)\b)|"
    r"(?:因为|由于|虽然|如果|除非)|"
    r"(?:并且|并|然后)(?:[^\r\n。！？.!?؟]{0,20}?再)?(?:请)?(?="
    r"(?:确认|选择|提供|回复|重新|重试|复核))",
    re.IGNORECASE,
)


def _bounded_referent(text: str) -> str | None:
    bounded = text[:160]
    boundary = _REFERENT_BREAK_RE.search(bounded)
    if boundary is not None:
        bounded = bounded[: boundary.start()]
    return _normalize_referent(bounded)


def _action_referent(match: re.Match[str], clause: _Clause) -> str | None:
    relative_end = match.end() - clause.start
    tail = clause.text[relative_end : relative_end + 160]
    return _bounded_referent(tail)


def _question_referent(question: re.Match[str], clause: _Clause) -> str | None:
    relative_start = question.start() - clause.start
    prefix = clause.text[max(0, relative_start - 160) : relative_start]
    english = re.search(
        r"\b(?:can|may|should|could)\s+I\s+"
        r"(?:proceed(?:\s+with)?|deploy|use|confirm|choose|select)\s*(.*?)\s*$",
        prefix,
        re.IGNORECASE,
    )
    if english is not None:
        return _bounded_referent(english.group(1))
    chinese = re.search(r"(?:部署|使用|选择|确认)([^，,;；。.!?！？؟]*)$", prefix)
    return _bounded_referent(chinese.group(1)) if chinese is not None else None


def _resolution_referent(match: re.Match[str], clause: _Clause) -> str | None:
    relative_end = match.end() - clause.start
    suffix = clause.text[relative_end : relative_end + 160]
    referent = re.match(r"\s+for\s+", suffix, re.IGNORECASE)
    if referent is not None:
        return _bounded_referent(suffix[referent.end() :])
    unsupported = re.match(r"\s+(?:from|by|for|on behalf of)\s+", suffix, re.IGNORECASE)
    if unsupported is not None:
        return _bounded_referent(suffix.strip())
    if re.search(r"(?:无需|不需要)", match.group()) is None:
        return None
    relative_start = match.start() - clause.start
    prefix = clause.text[max(0, relative_start - 80) : relative_start]
    chinese = re.search(r"([^\s，,;；。.!?！？؟]{1,40})\s*$", prefix)
    return _normalize_referent(chinese.group(1)) if chinese is not None else None


def _action_scopes(match: re.Match[str], clause: _Clause) -> FrozenSet[_EvidenceScope]:
    action = match.group().casefold()
    if re.search(
        r"(?:重新|重试|复核|操作|\b(?:retry|review)\b)", action, re.IGNORECASE
    ):
        return frozenset({_EvidenceScope.EXECUTION})
    if re.search(
        r"(?:选择|\bchoose\b|\bselect\b|\bchoice\b)", action, re.IGNORECASE
    ):
        return frozenset({_EvidenceScope.CHOICE})
    if re.search(
        r"(?:确认|\bconfirm(?:ation)?\b)", action, re.IGNORECASE
    ):
        return frozenset({_EvidenceScope.CONFIRMATION})
    if re.search(r"\b(?:reply|respond)\b", action, re.IGNORECASE) and re.search(
        r"\b(?:reply|respond)\b[^\r\n。！？.!?؟]{0,24}"
        r"(?:yes\s+or\s+no|proceed)",
        clause.text,
        re.IGNORECASE,
    ):
        return frozenset({_EvidenceScope.CONFIRMATION})
    if (
        re.search(r"(?:回复|\breply\b|\brespond\b)", action, re.IGNORECASE)
        and clause.communicative_choice
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


def _valid_resolution_matches(
    prose: str,
    clauses: Sequence[_Clause],
) -> List[re.Match[str]]:
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
            and match.start() >= clauses[clause_index].end
        ):
            clause_index += 1
        clause_start = clauses[clause_index].start if clauses else 0
        clause_end = clauses[clause_index].end if clauses else len(prose)
        prefix = prose[max(clause_start, match.start() - 120) : match.start()]
        suffix = prose[match.end() : min(clause_end, match.end() + 120)]
        invalid_suffix = _INVALID_RESOLUTION_SUFFIX_RE.search(suffix) is not None
        assistant_continuation = (
            _ASSISTANT_CONTINUATION_SUFFIX_RE.search(suffix) is not None
        )
        if _INVALID_RESOLUTION_PREFIX_RE.search(prefix) is None and (
            not invalid_suffix or assistant_continuation
        ):
            matches.append(match)
    return matches


def _question_evidence(
    block: _Block,
    prose: str,
    actions: Sequence[_Evidence],
    clauses: Sequence[_Clause],
) -> List[_Evidence]:
    evidence = []
    questions = list(_QUESTION_MARK_RE.finditer(prose))
    action_offsets = [action.offset for action in actions]
    clause_index = 0
    for index, question in enumerate(questions):
        while (
            clause_index + 1 < len(clauses)
            and question.start() >= clauses[clause_index].end
        ):
            clause_index += 1
        clause = clauses[clause_index] if clauses else None
        suffix_end = (
            questions[index + 1].start()
            if index + 1 < len(questions)
            else len(prose)
        )
        suffix = prose[question.end() : suffix_end]
        answer_cue = _ANSWER_CUE_RE.search(suffix)
        context_start = clauses[clause_index].start if clauses else 0
        context_end = suffix_end
        if answer_cue is not None and clauses:
            answer_offset = question.end() + answer_cue.start()
            answer_clause_index = clause_index
            while (
                answer_clause_index + 1 < len(clauses)
                and answer_offset >= clauses[answer_clause_index].end
            ):
                answer_clause_index += 1
            context_end = clauses[answer_clause_index].end
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
                    frozenset(
                        {clause.question_scope if clause else _EvidenceScope.REPLY}
                    ),
                    _question_referent(question, clause) if clause else None,
                )
            )
    return evidence


def _option_index(prose: str) -> _OptionIndex:
    matches = tuple(_OPTION_LINE_RE.finditer(prose))
    line_spans = []
    body_spans = []
    for index, match in enumerate(matches):
        line_end = prose.find("\n", match.start())
        if line_end < 0:
            line_end = len(prose)
        line_spans.append((match.start(), line_end))
        body_end = line_end
        cursor = line_end + 1
        next_option_start = (
            matches[index + 1].start() if index + 1 < len(matches) else len(prose)
        )
        while cursor < next_option_start:
            next_end = prose.find("\n", cursor)
            if next_end < 0:
                next_end = len(prose)
            line = prose[cursor:next_end]
            if re.match(r"[ \t]+\S", line) is None:
                break
            body_end = next_end
            cursor = next_end + 1
        body_spans.append((match.start(), body_end))
    return _OptionIndex(matches, tuple(line_spans), tuple(body_spans))


def _choice_evidence(
    blocks: Sequence[_Block],
    prose_blocks: Sequence[str],
    option_indexes: Sequence[_OptionIndex],
    resolutions: Sequence[_Evidence],
    questions_by_block: Sequence[Sequence[_Evidence]],
) -> List[_Evidence]:
    evidence = []
    choice_resolution_offsets = sorted(
        resolution.offset
        for resolution in resolutions
        if _EvidenceScope.CHOICE in resolution.scopes
    )
    cues_by_block = [
        list(_CHOICE_HANDOFF_CUE_RE.finditer(prose)) for prose in prose_blocks
    ]
    cue_starts_by_block = [
        [match.start() for match in matches] for matches in cues_by_block
    ]
    headings_by_block = [
        list(_OPTION_HEADING_RE.finditer(prose)) for prose in prose_blocks
    ]
    heading_starts_by_block = [
        [match.start() for match in matches] for matches in headings_by_block
    ]
    recommendations_by_block = [
        list(_RECOMMEND_LABEL_RE.finditer(prose)) for prose in prose_blocks
    ]
    recommendation_starts_by_block = [
        [match.start() for match in matches] for matches in recommendations_by_block
    ]
    option_matches_by_block = [index.matches for index in option_indexes]
    option_body_spans_by_block = [index.body_spans for index in option_indexes]
    option_body_starts_by_block = [
        [start for start, _ in spans] for spans in option_body_spans_by_block
    ]
    question_offsets_by_block = [
        [question.offset for question in questions]
        for questions in questions_by_block
    ]

    def in_option_body(block_index: int, offset: int) -> bool:
        spans = option_body_spans_by_block[block_index]
        starts = option_body_starts_by_block[block_index]
        span_index = bisect.bisect_right(starts, offset) - 1
        return span_index >= 0 and offset < spans[span_index][1]

    for index, prose in enumerate(prose_blocks):
        option_matches = option_matches_by_block[index]
        if not option_matches:
            continue
        option_line_spans = option_indexes[index].line_spans
        option_line_starts = [start for start, _ in option_line_spans]

        groups = []
        current = []
        current_labels = set()
        previous_line_end = -1
        previous_kind = None
        for match, (_, line_end) in zip(option_matches, option_line_spans):
            label = match.group(1).upper()
            label_kind = "number" if label.isdigit() else "letter"
            gap = prose[previous_line_end : match.start()] if current else ""
            separate = bool(current) and (
                re.fullmatch(
                    r"(?:\r?\n[ \t]+[^\r\n]*)*\r?\n[ \t]*", gap
                )
                is None
                or label in current_labels
                or label_kind != previous_kind
            )
            if separate:
                groups.append(current)
                current = []
                current_labels = set()
            current.append(match)
            current_labels.add(label)
            previous_line_end = line_end
            previous_kind = label_kind
        if current:
            groups.append(current)

        valid_groups = []
        for group in groups:
            labels = {match.group(1).upper() for match in group}
            if len(labels) >= 2:
                final_match_index = bisect.bisect_left(
                    option_line_starts, group[-1].start()
                )
                valid_groups.append(
                    (group, labels, option_line_spans[final_match_index][1])
                )

        for group_index, (group, labels, _) in enumerate(valid_groups):
            group_start = group[0].start()
            group_offset = blocks[index].offset + group_start
            local_lower = (
                valid_groups[group_index - 1][2]
                if group_index > 0
                else 0
            )
            local_upper = (
                valid_groups[group_index + 1][0][0].start()
                if group_index + 1 < len(valid_groups)
                else len(prose)
            )
            anchors = []
            if index > 0 and group_index == 0:
                anchors.extend(
                    question.offset
                    for question in questions_by_block[index - 1]
                    if question.offset < group_offset
                )
            if index + 1 < len(blocks) and group_index + 1 == len(valid_groups):
                anchors.extend(
                    question.offset
                    for question in questions_by_block[index + 1]
                    if _EvidenceScope.CHOICE in question.scopes
                )
            current_questions = questions_by_block[index]
            current_question_offsets = question_offsets_by_block[index]
            question_lower = bisect.bisect_left(
                current_question_offsets, blocks[index].offset + local_lower
            )
            question_upper = bisect.bisect_left(
                current_question_offsets, group_offset
            )
            anchors.extend(
                question.offset
                for question in current_questions[question_lower:question_upper]
            )
            trailing_question_lower = bisect.bisect_left(
                current_question_offsets, group_offset
            )
            trailing_question_upper = bisect.bisect_left(
                current_question_offsets, blocks[index].offset + local_upper
            )
            anchors.extend(
                question.offset
                for question in current_questions[
                    trailing_question_lower:trailing_question_upper
                ]
                if _EvidenceScope.CHOICE in question.scopes
            )
            nearby = range(max(0, index - 1), min(len(blocks), index + 2))
            for nearby_index in nearby:
                if nearby_index < index and group_index > 0:
                    continue
                if nearby_index > index and group_index + 1 < len(valid_groups):
                    continue
                if nearby_index == index:
                    cue_lower = bisect.bisect_left(
                        cue_starts_by_block[index], local_lower
                    )
                    cue_upper = bisect.bisect_left(
                        cue_starts_by_block[index], local_upper
                    )
                    nearby_cues = cues_by_block[index][cue_lower:cue_upper]
                else:
                    nearby_cues = cues_by_block[nearby_index]
                for cue in nearby_cues:
                    if in_option_body(nearby_index, cue.start()):
                        continue
                    anchors.append(blocks[nearby_index].offset + cue.start())
                if nearby_index == index:
                    heading_lower = bisect.bisect_left(
                        heading_starts_by_block[index], local_lower
                    )
                    heading_upper = bisect.bisect_left(
                        heading_starts_by_block[index], group_start
                    )
                    nearby_headings = headings_by_block[index][
                        heading_lower:heading_upper
                    ]
                else:
                    nearby_headings = headings_by_block[nearby_index]
                for heading in nearby_headings:
                    anchors.append(blocks[nearby_index].offset + heading.start())
                if nearby_index == index:
                    recommendation_lower = bisect.bisect_left(
                        recommendation_starts_by_block[index], local_lower
                    )
                    recommendation_upper = bisect.bisect_left(
                        recommendation_starts_by_block[index], local_upper
                    )
                    nearby_recommendations = recommendations_by_block[index][
                        recommendation_lower:recommendation_upper
                    ]
                else:
                    nearby_recommendations = recommendations_by_block[nearby_index]
                for recommendation in nearby_recommendations:
                    if in_option_body(nearby_index, recommendation.start()):
                        continue
                    if recommendation.group(1).upper() in labels:
                        anchors.append(
                            blocks[nearby_index].offset + recommendation.start()
                        )
            for anchor in sorted(anchors):
                lower, upper = sorted((group_offset, anchor))
                resolution_index = bisect.bisect_right(
                    choice_resolution_offsets, lower
                )
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
    clauses: Sequence[_Clause],
) -> List[_Evidence]:
    primary_matches = list(_REQUIRED_ACTION_RE.finditer(prose))
    clause_index = 0
    covered_primary_index = 0
    for match in _IMPERATIVE_ACTION_RE.finditer(prose):
        while (
            covered_primary_index < len(primary_matches)
            and primary_matches[covered_primary_index].end() <= match.start()
        ):
            covered_primary_index += 1
        if (
            covered_primary_index < len(primary_matches)
            and primary_matches[covered_primary_index].start() <= match.start()
            < primary_matches[covered_primary_index].end()
        ):
            continue
        while (
            clause_index + 1 < len(clauses)
            and match.start() >= clauses[clause_index].end
        ):
            clause_index += 1
        clause = clauses[clause_index] if clauses else None
        clause_start = clause.start if clause is not None else 0
        is_choose = re.search(r"(?:\b(?:choose|select)\b|选择)", match.group(), re.IGNORECASE)
        has_alternatives = (
            clause is not None
            and (
                clause.communicative_choice
                or re.search(r"\b\S+\s+or\s+\S+\b|或", clause.text, re.IGNORECASE)
                is not None
            )
        )
        if is_choose is not None and not has_alternatives:
            continue
        prefix_start = max(clause_start, match.start() - 160)
        prefix = prose[prefix_start : match.start()]
        boundaries = list(_IMPERATIVE_BOUNDARY_RE.finditer(prefix))
        if boundaries:
            prefix = prefix[boundaries[-1].end() :]
        elif prefix_start > clause_start:
            continue
        if re.fullmatch(r"\s*(?:[-*]\s*)?", prefix):
            primary_matches.append(match)
    primary_matches.sort(key=lambda item: item.start())
    primary_starts = [match.start() for match in primary_matches]
    action_matches = list(primary_matches)
    clause_index = 0
    for match in _COORDINATED_ACTION_RE.finditer(prose):
        while (
            clause_index + 1 < len(clauses)
            and match.start() >= clauses[clause_index].end
        ):
            clause_index += 1
        clause = clauses[clause_index] if clauses else None
        primary_index = bisect.bisect_left(primary_starts, match.start()) - 1
        if (
            clause is not None
            and primary_index >= 0
            and primary_starts[primary_index] >= clause.start
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
            and match.start() >= clauses[clause_index].end
        ):
            clause_index += 1
        clause = clauses[clause_index] if clauses else _Clause(
            prose,
            0,
            len(prose),
            _question_scope(prose),
            _COMMUNICATIVE_CHOICE_RE.search(prose) is not None,
        )
        evidence.append(
            _Evidence(
                block.offset + match.start(),
                _EvidenceKind.REQUIRED_ACTION,
                _TEXT_STRENGTH[_EvidenceKind.REQUIRED_ACTION],
                _action_scopes(match, clause),
                _action_referent(match, clause),
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
    clauses_by_block = [_clauses(prose) for prose in prose_blocks]
    option_indexes = [_option_index(prose) for prose in prose_blocks]
    evidence = []
    resolutions = []
    questions_by_block = [[] for _ in blocks]
    for block_index, (block, prose, clauses, option_index) in enumerate(
        zip(blocks, prose_blocks, clauses_by_block, option_indexes)
    ):
        courtesy_spans = _courtesy_spans(clauses)
        resolution_matches = _valid_resolution_matches(prose, clauses)
        resolution_spans = [(match.start(), match.end()) for match in resolution_matches]
        block_resolutions = []
        resolution_clause_index = 0
        for match in resolution_matches:
            while (
                resolution_clause_index + 1 < len(clauses)
                and match.start() >= clauses[resolution_clause_index].end
            ):
                resolution_clause_index += 1
            clause = clauses[resolution_clause_index] if clauses else _Clause(
                prose,
                0,
                len(prose),
                _question_scope(prose),
                _COMMUNICATIVE_CHOICE_RE.search(prose) is not None,
            )
            block_resolutions.append(
                _Evidence(
                    block.offset + match.start(),
                    _EvidenceKind.RESOLUTION,
                    0,
                    _resolution_scopes(match),
                    _resolution_referent(match, clause),
                )
            )
        evidence.extend(block_resolutions)
        resolutions.extend(block_resolutions)
        actions = _required_action_evidence(
            block,
            prose,
            courtesy_spans + list(option_index.body_spans) + resolution_spans,
            clauses,
        )
        evidence.extend(actions)
        block_questions = _question_evidence(block, prose, actions, clauses)
        evidence.extend(block_questions)
        questions_by_block[block_index].extend(block_questions)
    choices = _choice_evidence(
        blocks,
        prose_blocks,
        option_indexes,
        resolutions,
        questions_by_block,
    )
    if choices:
        choice_anchor_offsets = {choice.offset for choice in choices}
        evidence = [
            item
            for item in evidence
            if not (
                item.kind is _EvidenceKind.REQUIRED_ACTION
                and item.scopes == frozenset({_EvidenceScope.CHOICE})
                and item.offset in choice_anchor_offsets
            )
        ]
        evidence.extend(choices)
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

    waiting_evidence = []
    generic_cleared_at = {scope: -1 for scope in _EvidenceScope}
    referent_cleared_at = {}
    saw_resolution = False
    for item in _text_evidence(blocks):
        if item.kind is _EvidenceKind.RESOLUTION:
            for scope in item.scopes:
                if item.referent is None:
                    generic_cleared_at[scope] = item.offset
                else:
                    referent_cleared_at[(scope, item.referent)] = item.offset
            saw_resolution = True
        else:
            waiting_evidence.append(item)
    unresolved = [
        item
        for item in waiting_evidence
        if all(
            generic_cleared_at[scope] < item.offset
            and (
                item.referent is None
                or referent_cleared_at.get((scope, item.referent), -1) < item.offset
            )
            for scope in item.scopes
        )
    ]
    if unresolved:
        strongest = max(unresolved, key=lambda item: item.strength)
        return CompletionClassification("waiting", strongest.kind.value)
    if saw_resolution:
        return CompletionClassification("completed", "text_explicitly_resolved")
    return CompletionClassification("completed", "completion_only")
