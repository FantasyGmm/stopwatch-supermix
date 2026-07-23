"""Classify whether a completed Codex turn still needs user action."""

from __future__ import annotations

import dataclasses
import re
from typing import Any, List


WAITING_PHRASES = (
    "请确认",
    "请回复",
    "请提供",
    "请选择",
    "需要你",
    "可以吗",
    "请重新",
    "请复核",
    "告诉我",
    "确认后继续",
    "确认后我继续",
    "确认后我会继续",
    "等待你的",
    "等待回复",
    "等待确认",
    "需要进一步指令",
    "无法继续",
    "let me know",
    "please confirm",
    "please reply",
    "please provide",
    "please choose",
    "need your",
    "waiting for your",
    "cannot continue",
)

_MEMORY_CITATION_RE = re.compile(
    r"<oai-mem-citation>.*?</oai-mem-citation>", re.DOTALL | re.IGNORECASE
)
_FENCED_CODE_RE = re.compile(r"```.*?```", re.DOTALL)
_INLINE_CODE_RE = re.compile(r"`[^`\n]*`")
_MARKDOWN_LINK_TARGET_RE = re.compile(r"\]\([^)\n]*\)")
_URL_RE = re.compile(r"https?://\S+")
_QUOTED_QUESTION_RE = re.compile(
    r"(?:"
    r"“[^”\n]*[?？][^”\n]*”|"
    r"‘[^’\n]*[?？][^’\n]*’|"
    r"\"[^\"\n]*[?？][^\"\n]*\"|"
    r"'[^'\n]*[?？][^'\n]*'"
    r")"
)
_NON_PROSE_LINE_RE = re.compile(r"^\s*(?:#{1,6}\s+|>)")
_OPTION_RE = re.compile(
    r"^\s*(?:[-*]\s*)?([A-Z]|[1-9])\s*[.、:：)）]\s*\S+",
    re.MULTILINE,
)
_ENGLISH_CHOICE_CUE_RE = re.compile(
    r"\b(?:choose|select|pick|reply|respond)\b", re.IGNORECASE
)


@dataclasses.dataclass(frozen=True)
class CompletionClassification:
    outcome: str
    reason: str


def _strip_non_prose(message: str) -> str:
    text = _MEMORY_CITATION_RE.sub("", message)
    text = _FENCED_CODE_RE.sub("", text)
    text = _INLINE_CODE_RE.sub("", text)
    text = _MARKDOWN_LINK_TARGET_RE.sub("]", text)
    return _URL_RE.sub("", text)


def _paragraphs(message: str) -> List[str]:
    paragraphs = [
        part.strip()
        for part in _strip_non_prose(message).split("\n\n")
        if part.strip()
    ]
    while paragraphs and paragraphs[-1].startswith("::"):
        paragraphs.pop()
    return paragraphs


def _has_prose_question(paragraph: str) -> bool:
    prose = "\n".join(
        line for line in paragraph.splitlines() if not _NON_PROSE_LINE_RE.match(line)
    )
    prose = _QUOTED_QUESTION_RE.sub("", prose)
    return "?" in prose or "？" in prose


def _has_choice_handoff(message: str, final: str) -> bool:
    labels = []
    for match in _OPTION_RE.finditer(message):
        label = match.group(1).upper()
        if label not in labels:
            labels.append(label)
    if len(labels) < 2:
        return False

    final_upper = final.upper()
    mentioned = sum(
        bool(re.search(r"(?<![A-Z0-9]){}(?![A-Z0-9])".format(re.escape(label)), final_upper))
        for label in labels
    )
    if mentioned < 2:
        return False
    return "选" in final or bool(_ENGLISH_CHOICE_CUE_RE.search(final))


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

    paragraphs = _paragraphs(message)
    if not paragraphs:
        return CompletionClassification("completed", "completion_only")

    final = paragraphs[-1]
    if _has_prose_question(final):
        return CompletionClassification("waiting", "text_question")
    if len(paragraphs) >= 2 and _has_prose_question(paragraphs[-2]):
        return CompletionClassification("waiting", "text_question_before_epilogue")
    if _has_choice_handoff("\n\n".join(paragraphs), final):
        return CompletionClassification("waiting", "text_choice")

    lowered = final.casefold()
    if any(phrase in lowered for phrase in WAITING_PHRASES):
        return CompletionClassification("waiting", "text_required_action")
    return CompletionClassification("completed", "completion_only")
