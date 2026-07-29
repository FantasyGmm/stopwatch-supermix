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
    r"(?:请(?:确认|选择|提供|回复|重新|复核|批准|授权|验证)|"
    r"(?:你|您)(?:能否|能|可以)(?:请)?"
    r"(?:确认|选择|提供|回复|复核|批准|授权|验证)|"
    r"确认后(?:继续|我继续|我会继续)|"
    r"等待(?:你的)?(?:回复|确认)|"
    r"需要(?:你|您)的?(?:确认|选择|提供|回复|操作|批准|授权|验证)|"
    r"需要进一步指令|无法继续|"
    r"\bplease[ \t]+(?:confirm|choose|select|provide|reply|retry|review)\b|"
    r"\bplease[ \t]+(?:approve|authorize|verify)\b|"
    r"\bcan[ \t]+you[ \t]+(?:approve|authorize|confirm|choose|select|provide|"
    r"reply|retry|review|verify)\b|"
    r"\bwaiting[ \t]+for[ \t]+your[ \t]+"
    r"(?:reply|confirmation|choice|input|approval|authorization|verification)\b|"
    r"\bneed[ \t]+your[ \t]+"
    r"(?:confirmation|choice|input|reply|approval|authorization|verification)\b|"
    r"\bneed[ \t]+you[ \t]+to[ \t]+"
    r"(?:approve|authorize|confirm|choose|select|provide|reply|retry|review|verify)\b|"
    r"\b(?:we[ \t]+require|awaiting)[ \t]+your[ \t]+"
    r"(?:approval|authorization|verification)\b|"
    r"\b(?:(?!(?:and|although|because|no|not|but)\b)[A-Za-z-]+[ \t]+){0,3}"
    r"(?:approval|authorization|verification)[ \t]+"
    r"(?:is|remains)[ \t]+"
    r"(?:(?:still|also|not[ \t]+only)[ \t]+)?required\b|"
    r"\bblocked[ \t]+pending[ \t]+(?:[A-Za-z-]+[ \t]+){0,2}"
    r"(?:approval|authorization|verification)\b|"
    r"\byou[ \t]+(?:must|need[ \t]+to)[ \t]+"
    r"(?:approve|authorize|confirm|choose|select|provide|reply|retry|review|verify)\b|"
    r"\bcannot[ \t]+continue\b|"
    r"\blet[ \t]+me[ \t]+know\b|告诉我)",
    re.IGNORECASE,
)
_ATTRIBUTED_ACTION_PREFIX_RE = re.compile(
    r"(?:\b(?:docs?|documentation|examples?|logs?|incident[ \t]+report)\b"
    r"[^\r\n。！？.!?؟]{0,40}\b(?:say|says|state|states|said|"
    r"indicate|indicates|show|shows|suggest|suggests)\b|"
    r"\b(?:according[ \t]+to|per)[ \t]+"
    r"(?:(?:the|our|your|their|its)[ \t]+)?"
    r"(?:[A-Za-z-]+[ \t]+){0,2}"
    r"(?:docs?|documentation|examples?|logs?|incident[ \t]+report)\b)",
    re.IGNORECASE,
)
_ATTRIBUTED_REQUIREMENT_HISTORY_SUFFIX_RE = re.compile(
    r"^[ \t]+in[ \t]+(?:the[ \t]+)?"
    r"(?:old|previous|prior|historical)[ \t]+workflow\b",
    re.IGNORECASE,
)
_ATTRIBUTION_SCOPE_BREAK_RE = re.compile(
    r"\b(?:but|however|then|yet)\b|"
    r"(?:[,;][ \t]*|\band[ \t]+)(?:I|we|you)\b|"
    r"[;；][ \t]*(?=(?:please[ \t]+)?"
    r"(?:approve|authorize|confirm|choose|select|provide|reply|retry|review|verify)\b)|"
    r"\band[ \t]+(?=please[ \t]+"
    r"(?:approve|authorize|confirm|choose|select|provide|reply|retry|review|verify)\b)|"
    r"(?:[;；]|\b(?:and|although)\b)[ \t]*(?="
    r"(?:approval|authorization|verification)[ \t]+"
    r"(?:is|remains)[ \t]+(?:(?:still|also)[ \t]+)?required\b)",
    re.IGNORECASE,
)
_POLARITY_SCOPE_BREAK_RE = re.compile(
    r"\b(?:and|although|because|but|however|yet)\b|[;；]",
    re.IGNORECASE,
)
_NEGATED_ACTION_PREFIX_RE = re.compile(
    r"\b(?:no|not(?![ \t]+only\b))[ \t]+"
    r"(?:[A-Za-z-]+[ \t]+){0,4}$",
    re.IGNORECASE,
)
_NEGATED_ACTION_MATCH_RE = re.compile(
    r"\b(?:no|not(?![ \t]+only\b))\b", re.IGNORECASE
)
_NON_USER_OWNERSHIP_SUFFIX_RE = re.compile(
    r"^\s*[,;]?\s*but[ \t]+not[ \t]+from[ \t]+you\b"
    r"(?=[ \t]*(?:[.!?]|$))",
    re.IGNORECASE,
)
_IMPERATIVE_ACTION_RE = re.compile(
    r"\b(?:approve|authorize|confirm|choose|select|provide|"
    r"reply(?:\s+with)?|retry|review|verify)\b|"
    r"(?:确认|选择|提供|回复|重新|重试|复核)",
    re.IGNORECASE,
)
_IMPERATIVE_BOUNDARY_RE = re.compile(r"[;；:：]|[ \t]+[—–-][ \t]+")
_ACTION_LIST_PREFIX_RE = re.compile(
    r"^\s*(?:[-*][ \t]*)?(?:"
    r"required[ \t]+actions?|"
    r"to[ \t]+do|"
    r"remaining[ \t]+steps?|"
    r"(?:the[ \t]+)?next[ \t]+steps?(?:[ \t]+are)?|"
    r"next[ \t]+actions?|"
    r"please[ \t]+complete[ \t]+these[ \t]+steps|"
    r"steps[ \t]+to[ \t]+complete|"
    r"tasks?[ \t]+remaining|"
    r"action[ \t]+items?|"
    r"actions?[ \t]+required)\s*$",
    re.IGNORECASE,
)
_IMPERATIVE_OBJECT_RE = re.compile(
    r"^[ \t]+(?:(?:the|an?|this|that|these|those|your|our)[ \t]+)?"
    r"[A-Za-z0-9][A-Za-z0-9_-]*\b|"
    r"^[\u3400-\u9fff]{1,16}",
    re.IGNORECASE,
)
_REPORT_CONTINUATION_RE = re.compile(
    r"^[ \t]+of[ \t]+[^\r\n。！？.!?؟]{1,48}[ \t]+"
    r"(?:(?:is|are|was|were)[ \t]+"
    r"(?:successful|complete(?:d)?|finished|pending|failed)|"
    r"(?:passed|succeeded|failed|completed|finished))\b|"
    r"^[ \t]+(?:successful|finished|complete(?:d)?|passed|pending|failed)\b"
    r"(?:[ \t]+(?:internally|automatically|successfully))?"
    r"(?:[ \t]+all[ \t]+tests)?"
    r"(?:[ \t]+(?:without[ \t]+(?:issues?|errors?|problems?|warnings?)|"
    r"with[ \t]+no[ \t]+(?:issues?|errors?|problems?|warnings?)))?"
    r"[ \t]*(?:[.!?]|$)|"
    r"^[ \t]+(?:(?:the|an?|this|that|these|those|your|our)[ \t]+)?"
    r"[A-Za-z0-9][A-Za-z0-9_-]*[ \t]+"
    r"(?:(?:is|are|was|were|has|have|had)\b|"
    r"(?:passed|succeeded|failed|completed|finished)\b)|"
    r"^(?:成功|完成|通过|失败|待定|处理中)",
    re.IGNORECASE,
)
_REPORT_NOUN_SUFFIX_RE = re.compile(
    r"^\s*(?:[,，](?!\s*(?:(?:then[ \t]+)?"
    r"(?:confirm|choose|select|provide|reply|retry|review)\b|"
    r"(?:然后)?(?:确认|选择|提供|回复|重新|重试|复核)))|"
    r"\bcomplete(?:d)?\b\s*[.!?。！？]|"
    r"\b(?:is|are|was|were|has|have)\b|"
    r"\band\b(?![ \t]+(?:confirm|choose|select|provide|reply|retry|review)\b))",
    re.IGNORECASE,
)
_COORDINATED_ACTION_RE = re.compile(
    r"(?:\b(?:and|or|then)\s+|,[ \t]+)(?:also\s+)?"
    r"(?:approve|authorize|confirm|choose|select|provide|reply|retry|review|verify)\b|"
    r"(?:并且|并|然后|和|及|或|、|，)(?:[^\r\n。！？.!?؟]{0,20}?再)?(?:请)?"
    r"(?:确认|选择|提供|回复|重新|重试|复核|批准|授权|验证)",
    re.IGNORECASE,
)
_COORDINATED_REQUIREMENT_NOUN_RE = re.compile(
    r"(?:\b(?:and|or)[ \t]+(?:also[ \t]+)?|,[ \t]*)"
    r"(?:your[ \t]+)?"
    r"(?:approval|authorization|confirmation|choice|input|reply|verification)\b",
    re.IGNORECASE,
)
_OWNED_REQUIREMENT_RE = re.compile(
    r"\b(?:waiting[ \t]+for[ \t]+your|awaiting[ \t]+your|"
    r"need[ \t]+your|require[ \t]+your)[ \t]+"
    r"(?:approval|authorization|confirmation|choice|input|reply|verification)\b",
    re.IGNORECASE,
)
_COMMUNICATIVE_CHOICE_RE = re.compile(
    r"\b[A-Z1-9]\b[^\r\n。！？.!?؟]{0,20}\b(?:or|and)\b"
    r"[^\r\n。！？.!?؟]{0,20}\b[A-Z1-9]\b|"
    r"[A-Z1-9]\s*[、，,或]\s*[A-Z1-9]",
    re.IGNORECASE,
)
_ALTERNATIVE_CHOICE_RE = re.compile(
    r"^\s*[A-Z1-9]\s*(?:or|或|还是)\s*[A-Z1-9]\s*[?？؟]?[.!。]?\s*$|"
    r"\b(?:do|will|would|can|could|should|may)[ \t]+"
    r"(?:you|we|I)[ \t]+[^\r\n。！？.!?؟]{0,40}"
    r"\b(?:prefer|choose|select|pick)\b"
    r"[^\r\n。！？.!?؟]{0,80}\bor\b|"
    r"\b(?:(?:should|will|would)[ \t]+(?:you|we|I)|"
    r"(?:may|could)[ \t]+(?:I|we))[ \t]+"
    r"[^\r\n。！？.!?؟]{0,40}\buse\b"
    r"[^\r\n。！？.!?؟]{0,80}\bor\b|"
    r"^\s*use\b[^\r\n。！？.!?؟]{0,80}\bor\b|"
    r"(?:你|您)[^\r\n。！？.!?؟]{0,12}"
    r"(?:要|希望|想|使用|选择|选)[^\r\n。！？.!?؟]{0,16}"
    r"还是[^\r\n。！？.!?؟]{1,16}|"
    r"^\s*(?:生产|预发|开发|测试)[ \t]*还是[ \t]*"
    r"(?:生产|预发|开发|测试)\s*[？?]?[。.]?\s*$|"
    r"^\s*(?:继续|停止)[ \t]*还是[ \t]*"
    r"(?:继续|停止)\s*[？?]?[。.]?\s*$|"
    r"^\s*(?:开[ \t]*还是[ \t]*关|关[ \t]*还是[ \t]*开)"
    r"\s*[？?]?[。.]?\s*$|"
    r"^\s*(?:是[ \t]*)?"
    r"(?:上线|回滚|开启|关闭|保留|删除|提交|撤回)[ \t]*还是[ \t]*"
    r"(?:上线|回滚|开启|关闭|保留|删除|提交|撤回)"
    r"\s*[？?]?[。.]?\s*$|"
    r"^\s*(?:甲|乙)[ \t]*还是[ \t]*(?:甲|乙)"
    r"\s*[？?]?[。.]?\s*$|"
    r"^\s*(?![\u3400-\u9fff]{0,12}(?:使用|采用|报告))"
    r"[\u3400-\u9fff]{1,12}(?:环境|方案|选项|版本|模式|版|色)"
    r"[ \t]*还是[ \t]*"
    r"[\u3400-\u9fff]{1,12}(?:环境|方案|选项|版本|模式|版|色)"
    r"\s*[？?]?[。.]?\s*$",
    re.IGNORECASE,
)
_COURTESY_CLAUSE_RE = re.compile(
    r"(?:^\s*(?:[-*]\s*)?(?:"
    r"if[ \t]+you(?:['’]d|[ \t]+would)[ \t]+like\b|"
    r"if[ \t]+you[ \t]+(?:want|need)[ \t]+(?:more|additional|further)\b|"
    r"如果你(?:愿意|想(?:要)?(?:了解)?更多)|"
    r"如(?:果)?需(?:要)?更多)"
    r"|\blet[ \t]+me[ \t]+know[ \t]+if[ \t]+"
    r"you(?:['’]d|[ \t]+would)[ \t]+like[ \t]+"
    r"(?:more|additional|further)\b|"
    r"\blet[ \t]+me[ \t]+know[ \t]+if[ \t]+you[ \t]+"
    r"(?:want|need)[ \t]+"
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
    r"|(?P<security_list>"
    r"\bno (?:approval|authorization|verification)"
    r"(?:,[ \t]*(?:(?:and|or)[ \t]+)?|[ \t]+(?:and|or)[ \t]+)"
    r"(?:approval|authorization|verification)"
    r"(?:(?:,[ \t]*(?:(?:and|or)[ \t]+)?|[ \t]+(?:and|or)[ \t]+)"
    r"(?:approval|authorization|verification))*[ \t]+(?:is|are)[ \t]+required\b|"
    r"\bneither[ \t]+(?:approval|authorization|verification)"
    r"(?:,[ \t]*(?:approval|authorization|verification))*"
    r",?[ \t]+nor[ \t]+(?:approval|authorization|verification)"
    r"[ \t]+(?:is|are)[ \t]+required\b|"
    r"\b(?:approval|authorization|verification)"
    r"(?:(?:,[ \t]*(?:(?:and|or)[ \t]+)?|[ \t]+(?:and|or)[ \t]+)"
    r"(?:approval|authorization|verification))+"
    r"[ \t]+are[ \t]+(?:no[ \t]+longer|not)[ \t]+required\b|"
    r"(?:批准|授权|验证)"
    r"(?:(?:[、，,][ \t]*(?:或|及|和|并|与|以及)?|"
    r"(?:或|及|和|并|与|以及))(?:批准|授权|验证))+不再需要|"
    r"(?:无需|不需要)(?:(?:你|您)的?)?"
    r"(?:批准|授权|验证)"
    r"(?:(?:[、，,][ \t]*(?:或|及|和|并|与|以及)?|"
    r"(?:或|及|和|并|与|以及))"
    r"(?:批准|授权|验证))+)"
    r"|(?P<approval>"
    r"(?:无需|不需要)(?:(?:你|您)的?)?批准|"
    r"\b(?:no approval is required|approval is (?:no longer|not) required)\b)"
    r"|(?P<authorization>"
    r"(?:无需|不需要)(?:(?:你|您)的?)?授权|"
    r"\b(?:no authorization is required|"
    r"authorization is (?:no longer|not) required)\b)"
    r"|(?P<verification>"
    r"(?:无需|不需要)(?:(?:你|您)的?)?验证|"
    r"\b(?:no verification is required|"
    r"verification is (?:no longer|not) required)\b)"
    r"|(?P<all>"
    r"(?:无需|不需要)(?:你|您)?操作|"
    r"\bno (?:user )?action is required\b|"
    r"\bno action remains\b)",
    re.IGNORECASE,
)
_INVALID_RESOLUTION_PREFIX_RE = re.compile(
    r"(?:\b(?:is not|isn't|was not|wasn't|not) (?:true|the case) that\b|"
    r"\b(?:do not|don't) assume\b|"
    r"\b(?:docs?|documentation|examples?|logs?|incident[ \t]+report)\b"
    r"(?:[ \t]+[A-Za-z-]+){0,2}[ \t]+"
    r"(?:say|says|state|states|said|indicate|indicates|show|shows|"
    r"suggest|suggests)\b|"
    r"\b(?:according[ \t]+to|per)[ \t]+"
    r"(?:(?:the|our|your|their|its)[ \t]+)?"
    r"(?:[A-Za-z-]+[ \t]+){0,2}"
    r"(?:docs?|documentation|examples?|logs?|incident[ \t]+report)\b|"
    r"\b(?:it[ \t]+(?:is(?:[ \t]+not)?|isn['’]t)[ \t]+expected[ \t]+that|"
    r"(?:I|we|they)[ \t]+(?:(?:(?:do|does)[ \t]+not|"
    r"(?:don|doesn)['’]t)[ \t]+)?"
    r"(?:expect|expects|anticipate|anticipates)(?:[ \t]+that)?)\b|"
    r"\b(?:after|once|when|if|until|then)\b|"
    r"(?:并非|不是|文档(?:说|写道|指出)|"
    r"根据[\u4e00-\u9fff]{0,8}文档|之后|以后|如果|待))",
    re.IGNORECASE,
)
_INVALID_RESOLUTION_SUFFIX_RE = re.compile(
    r"(?:\b(?:after|once|when|if|until)\b|"
    r"\bto[ \t]+be\b|"
    r"\b(?:according[ \t]+to|per)[ \t]+"
    r"(?:(?:the|our|your|their|its)[ \t]+)?"
    r"(?:[A-Za-z-]+[ \t]+){0,2}"
    r"(?:docs?|documentation|examples?|logs?|incident[ \t]+report)\b|"
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
    r"^(?:(?:[A-Za-z0-9][A-Za-z0-9 -]{0,40}[ \t]+)?options"
    r"(?:[ \t]+(?:are|available))?[ \t]*:|"
    r"[\u4e00-\u9fff0-9０-９ \t]{0,20}(?:选项|方案)[ \t]*[：:])\s*$",
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
    APPROVAL = "approval"
    AUTHORIZATION = "authorization"
    VERIFICATION = "verification"
    EXECUTION = "execution"


_REPLY_RESOLUTION_SCOPES = frozenset(
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
    question_scopes: FrozenSet[_EvidenceScope]
    communicative_choice: bool
    has_question_mark: bool = False
    question_actions: Tuple[Tuple[_EvidenceScope, int], ...] = ()
    question_action_ends: Tuple[int, ...] = ()
    question_action_referents: Tuple[str | None, ...] = ()
    question_complement_range: Tuple[int, int] | None = None


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


_QUESTION_GOVERNING_ACTION_RE = re.compile(
    r"(?:\b(?:can|could|do|does|did|would|will|may|should)[ \t]+you[ \t]+"
    r"(?:please[ \t]+)?|^\s*(?:please[ \t]+)?)"
    r"(?P<english>approve|authorize|verify|confirm)\b|"
    r"(?:(?:你|您)(?:能否|能|可以)?(?:请)?|^\s*请)"
    r"(?P<chinese>批准|授权|验证|确认)",
    re.IGNORECASE,
)
_COORDINATED_QUESTION_ACTION_RE = re.compile(
    r"(?:\b(?:and|or|then)[ \t]+|,[ \t]+(?:and[ \t]+|then[ \t]+)?)"
    r"(?:also[ \t]+)?"
    r"(?P<english>approve|authorize|verify|confirm)\b|"
    r"(?:并且|并|然后|和|及|或|、|，)(?:请)?"
    r"(?P<chinese>批准|授权|验证|确认)",
    re.IGNORECASE,
)
_QUESTION_COMPLEMENT_RE = re.compile(
    r"\b(?:that|whether|if)\b|(?:是否)",
    re.IGNORECASE,
)
_QUESTION_ACTION_PATTERN = (
    r"(?:approve|authorize|confirm|choose|select|provide|reply|retry|review|verify)"
)
_CHINESE_QUESTION_ACTION_PATTERN = (
    r"(?:确认|选择|提供|回复|重新|重试|复核|批准|授权|验证)"
)
_QUESTION_EXPLICIT_OUTER_ACTION_RE = re.compile(
    rf"(?:,[ \t]*)?(?:and|then)[ \t]+"
    rf"(?:(?:please)[ \t]+|(?:can|could|would|will)[ \t]+you[ \t]+"
    rf"(?:please[ \t]+)?)(?={_QUESTION_ACTION_PATTERN}\b)|"
    rf"(?:，)?(?:并且|并|然后)(?:请|你能|您能|你可以|您可以)"
    rf"(?={_CHINESE_QUESTION_ACTION_PATTERN})",
    re.IGNORECASE,
)
_QUESTION_COMPLEMENT_PREDICATE_RE = re.compile(
    r"\b(?:the[ \t]+)?[A-Za-z0-9][A-Za-z0-9_-]*"
    r"(?:[ \t]+[A-Za-z0-9][A-Za-z0-9_-]*){0,3}[ \t]+"
    r"(?:is|are|was|were|remains?)[ \t]+(?:(?:still|also)[ \t]+)?"
    r"(?:approved|authorized|available|complete|completed|ready|required|"
    r"successful|verified)\b|"
    r"\b(?:the[ \t]+)?[A-Za-z0-9][A-Za-z0-9_-]*"
    r"(?:[ \t]+[A-Za-z0-9][A-Za-z0-9_-]*){0,3}[ \t]+"
    r"(?:complete|completed|fail|failed|finish|finished|pass|passed|"
    r"succeed|succeeded)\b|"
    r"(?:是否)?(?:需要|需)(?:批准|授权|确认|验证)|"
    r"(?:通过|完成|就绪|成功|失败)",
    re.IGNORECASE,
)
_QUESTION_COMPLETED_OUTER_ACTION_RE = re.compile(
    rf"(?:,[ \t]*)?(?:and|then)[ \t]+(?:also[ \t]+)?(?:please[ \t]+)?"
    rf"(?={_QUESTION_ACTION_PATTERN}\b)|"
    rf"(?:，)?(?:并且|并|然后)(?:请)?(?={_CHINESE_QUESTION_ACTION_PATTERN})",
    re.IGNORECASE,
)
_QUESTION_ACTION_STATE_SUBJECT_RE = re.compile(
    rf"{_QUESTION_ACTION_PATTERN}\b[ \t]+"
    r"(?:is|are|was|were|remains?)\b",
    re.IGNORECASE,
)


def _question_action_scope(action: str) -> _EvidenceScope:
    normalized = action.casefold()
    if normalized in {"approve", "批准"}:
        return _EvidenceScope.APPROVAL
    if normalized in {"authorize", "授权"}:
        return _EvidenceScope.AUTHORIZATION
    if normalized in {"verify", "验证"}:
        return _EvidenceScope.VERIFICATION
    return _EvidenceScope.CONFIRMATION


def _question_complement_range(
    clause_text: str, actions: Sequence[Tuple[_EvidenceScope, int]]
) -> Tuple[int, int] | None:
    if not actions:
        return None
    governing_end = actions[0][1]
    complement = _QUESTION_COMPLEMENT_RE.search(clause_text, governing_end)
    if complement is None:
        return None
    if (
        complement.group().casefold() == "if"
        and clause_text[governing_end : complement.start()].strip()
    ):
        return None
    inner_subject = _has_inner_complement_subject(
        clause_text,
        governing_end,
        complement.start(),
    )
    restart = _question_complement_restart(
        clause_text,
        complement.end(),
        allow_completed=not inner_subject,
    )
    return (
        complement.start(),
        restart.start() if restart is not None else len(clause_text),
    )


def _has_inner_complement_subject(
    clause_text: str,
    subject_start: int,
    complement_start: int,
) -> bool:
    subject = clause_text[subject_start:complement_start]
    return (
        re.search(
            r"(?:我|我们|你|你们|您|他|她|他们|她们|"
            r"[\u4e00-\u9fff]{0,8}(?:团队|小组|人员|成员|用户|客户|"
            r"同事|工程师|审核员|负责人))[ \t]*$",
            subject,
        )
        is not None
    )


def _question_complement_restart(
    clause_text: str,
    complement_end: int,
    *,
    allow_completed: bool = True,
) -> re.Match[str] | None:
    explicit = _QUESTION_EXPLICIT_OUTER_ACTION_RE.search(
        clause_text, complement_end
    )
    predicate = _QUESTION_COMPLEMENT_PREDICATE_RE.search(
        clause_text, complement_end
    )
    completed = None
    if predicate is not None and allow_completed:
        for candidate in _QUESTION_COMPLETED_OUTER_ACTION_RE.finditer(
            clause_text, predicate.end()
        ):
            if _QUESTION_ACTION_STATE_SUBJECT_RE.match(
                clause_text, candidate.end()
            ) is None:
                completed = candidate
                break
    if explicit is None:
        return completed
    if completed is None or explicit.start() < completed.start():
        return explicit
    return completed


def _governing_question_actions(
    action_prose: str,
) -> Tuple[Tuple[_EvidenceScope, int], ...]:
    governing_action = _QUESTION_GOVERNING_ACTION_RE.search(action_prose)
    if governing_action is None:
        return ()
    action = governing_action.group("english") or governing_action.group("chinese")
    actions = [(_question_action_scope(action), governing_action.end())]
    cursor = governing_action.end()
    while coordinated := _COORDINATED_QUESTION_ACTION_RE.search(
        action_prose, cursor
    ):
        intervening = action_prose[cursor : coordinated.start()]
        complement = _QUESTION_COMPLEMENT_RE.search(intervening)
        if (
            complement is not None
            and complement.group().casefold() == "if"
            and intervening[: complement.start()].strip()
        ):
            complement = None
        if complement is not None:
            complement_end = cursor + complement.end()
            inner_subject = _has_inner_complement_subject(
                action_prose,
                cursor,
                cursor + complement.start(),
            )
            restart = _question_complement_restart(
                action_prose,
                complement_end,
                allow_completed=not inner_subject,
            )
            if restart is None or coordinated.start() < restart.start():
                break
        action = coordinated.group("english") or coordinated.group("chinese")
        if coordinated.group("chinese") and re.match(
            r"状态", action_prose[coordinated.end() :]
        ):
            break
        actions.append((_question_action_scope(action), coordinated.end()))
        cursor = coordinated.end()
    return tuple(actions)


def _question_scopes(
    prose: str,
    actions: Sequence[Tuple[_EvidenceScope, int]] | None = None,
    action_prose: str | None = None,
) -> FrozenSet[_EvidenceScope]:
    if action_prose is None:
        action_prose = _QUOTED_TEXT_RE.sub(
            lambda match: " " * len(match.group()), prose
        )
    if actions is None:
        actions = _governing_question_actions(action_prose)
    if actions:
        return frozenset(scope for scope, _ in actions)
    if _ALTERNATIVE_CHOICE_RE.search(prose) is not None or re.search(
        r"(?:哪(?:个|种)|如何|怎么|\bwhich\b|\bwhat option\b)",
        prose,
        re.IGNORECASE,
    ):
        return frozenset({_EvidenceScope.CHOICE})
    if re.search(r"(?:确认|\bconfirm)", action_prose, re.IGNORECASE):
        return frozenset({_EvidenceScope.CONFIRMATION})
    if re.search(
        r"(?:\b(?:can|may|should|could) I\b|(?:可以|继续)吗)",
        action_prose,
        re.IGNORECASE,
    ):
        return frozenset({_EvidenceScope.CONFIRMATION})
    return frozenset({_EvidenceScope.REPLY})


def _clauses(prose: str) -> List[_Clause]:
    clauses = []
    for match in _CLAUSE_RE.finditer(prose):
        text = match.group()
        action_prose = _QUOTED_TEXT_RE.sub(
            lambda quoted: " " * len(quoted.group()), text
        )
        actions = _governing_question_actions(action_prose)
        group_ends = _question_action_group_ends(text, actions)
        group_referents = {}
        previous_group_last = None
        for group_last in group_ends:
            if group_last != previous_group_last:
                group_referents[group_last] = _governing_question_referent(
                    text, actions, group_last, group_last
                )
                previous_group_last = group_last
        clauses.append(
            _Clause(
                text=text,
                start=match.start(),
                end=match.end(),
                question_scopes=_question_scopes(text, actions, action_prose),
                communicative_choice=_COMMUNICATIVE_CHOICE_RE.search(text)
                is not None,
                has_question_mark=_QUESTION_MARK_RE.search(text) is not None,
                question_actions=actions,
                question_action_ends=tuple(end for _, end in actions),
                question_action_referents=tuple(
                    group_referents[group_last] for group_last in group_ends
                ),
                question_complement_range=_question_complement_range(
                    text, actions
                ),
            )
        )
    return clauses


def _normalize_referent(text: str) -> str | None:
    referent = re.sub(r"\s+", " ", text).strip().casefold()
    if referent.endswith(("吗", "呢")):
        referent = referent[:-1].rstrip()
    referent = re.sub(r"^(?:to|into|onto|on|in|with|for)\s+", "", referent)
    referent = re.sub(r"^(?:the|a|an)\s+", "", referent)
    return referent or None


_REFERENT_BREAK_RE = re.compile(
    r"[，,;；。.!?！？؟]|"
    r"\b(?:because|since|although|though|while|unless|if)\b|"
    r"\b(?:and|or|then)\s+(?:also\s+)?(?="
    r"(?:approve|authorize|confirm|choose|select|provide|reply|retry|review|verify)\b)|"
    r"\bplease\s+(?=(?:approve|authorize|confirm|choose|select|provide|"
    r"reply|retry|review|verify)\b)|"
    r"(?:因为|由于|虽然|如果|除非)|"
    r"(?:并且|并|然后|和|及|或|、|，)(?:[^\r\n。！？.!?؟]{0,20}?再)?(?:请)?(?="
    r"(?:确认|选择|提供|回复|重新|重试|复核|批准|授权|验证))",
    re.IGNORECASE,
)


def _bounded_referent(text: str) -> str | None:
    bounded = text[:160]
    boundary = _REFERENT_BREAK_RE.search(bounded)
    if boundary is not None:
        bounded = bounded[: boundary.start()]
    return _normalize_referent(bounded)


def _action_referent(match: re.Match[str], clause: _Clause) -> str | None:
    relative_start = match.start() - clause.start
    if re.search(
        r"需要(?:你|您)的?(?:批准|授权|验证)", match.group()
    ) is not None:
        prefix = clause.text[max(0, relative_start - 80) : relative_start]
        chinese = re.search(r"([^\s，,;；。.!?！？؟]{1,40})\s*$", prefix)
        if chinese is not None and chinese.group(1) not in {"我", "我们"}:
            return _normalize_referent(chinese.group(1))
    relative_end = match.end() - clause.start
    tail = clause.text[relative_end : relative_end + 160]
    referent = _bounded_referent(tail)
    required_target = re.match(
        r"\s+(?:is|are)[ \t]+required[ \t]+for[ \t]+(.+)",
        tail,
        re.IGNORECASE,
    )
    if required_target is not None:
        referent = _bounded_referent(required_target.group(1))
    if referent is None:
        coordinated = list(_COORDINATED_ACTION_RE.finditer(tail))
        if coordinated:
            group_last = coordinated[0]
            for candidate in coordinated[1:]:
                between = tail[group_last.end() : candidate.start()]
                if (
                    re.search(
                        r"\bthen\b|然后", candidate.group(), re.IGNORECASE
                    )
                    or _bounded_referent(between) is not None
                ):
                    break
                group_last = candidate
            referent = _bounded_referent(
                tail[group_last.end() : group_last.end() + 160]
            )
    return referent


def _question_referent(
    question: re.Match[str], clause: _Clause, scope: _EvidenceScope
) -> str | None:
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


def _governing_question_referent(
    clause_text: str,
    actions: Sequence[Tuple[_EvidenceScope, int]],
    action_index: int,
    group_last: int,
) -> str | None:
    _, action_end = actions[action_index]
    tail = clause_text[action_end : action_end + 200]
    stripped_tail = tail.lstrip()
    if not stripped_tail or stripped_tail.startswith(
        (",", "，", ";", "；", ".", "。", "?", "？", "!", "！", "؟")
    ):
        return None
    complement = _QUESTION_COMPLEMENT_RE.search(tail)
    if (
        complement is not None
        and complement.group().casefold() == "if"
        and tail[: complement.start()].strip()
    ):
        complement = None
    next_action = _COORDINATED_QUESTION_ACTION_RE.search(tail)
    if complement is not None and (
        next_action is None or complement.start() < next_action.start()
    ):
        target = re.search(
            r"\bfor[ \t]+([^\r\n。！？.!?؟]+)", tail, re.IGNORECASE
        )
        if target is not None:
            return _bounded_referent(target.group(1))
        english_subject = re.match(
            r"\s*(?:whether|if)[ \t]+(.+?)[ \t]+requires?\b",
            tail,
            re.IGNORECASE,
        )
        if english_subject is not None:
            return _bounded_referent(english_subject.group(1))
        chinese_subject = re.match(r"\s*(.+?)是否(?:需要|需)", tail)
        return (
            _bounded_referent(chinese_subject.group(1))
            if chinese_subject is not None
            else None
        )
    chinese_status = re.match(r"\s*([^，,;；。.!?！？؟]+状态)(?:吗|呢)?", tail)
    referent = (
        _normalize_referent(chinese_status.group(1))
        if chinese_status is not None
        else _bounded_referent(tail)
    )
    if referent is None and action_index + 1 < len(actions):
        group_end = actions[group_last][1]
        referent = _bounded_referent(clause_text[group_end : group_end + 160])
    return referent


def _question_action_group_ends(
    clause_text: str, actions: Sequence[Tuple[_EvidenceScope, int]]
) -> Tuple[int, ...]:
    group_ends = [0] * len(actions)
    group_start = 0
    for action_index in range(1, len(actions)):
        between = clause_text[
            actions[action_index - 1][1] : actions[action_index][1]
        ]
        starts_new_group = (
            re.search(r"\bthen\b|然后", between, re.IGNORECASE) is not None
            or _bounded_referent(between) is not None
        )
        if starts_new_group:
            for member in range(group_start, action_index):
                group_ends[member] = action_index - 1
            group_start = action_index
    for member in range(group_start, len(actions)):
        group_ends[member] = len(actions) - 1
    return tuple(group_ends)


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
    if re.search(r"(?:批准|授权|验证)", match.group()) is not None:
        chinese_suffix = re.match(r"([^\s，,;；。.!?！？؟]{1,40})", suffix)
        if chinese_suffix is not None:
            stripped_suffix = re.sub(
                r"(?:即可继续了|即可继续|了)$", "", chinese_suffix.group(1)
            )
            if stripped_suffix:
                return _normalize_referent(stripped_suffix)
    relative_start = match.start() - clause.start
    prefix = clause.text[max(0, relative_start - 80) : relative_start]
    chinese = re.search(r"([^\s，,;；。.!?！？؟]{1,40})\s*$", prefix)
    if chinese is not None and chinese.group(1).endswith(("也", "且")):
        stripped = chinese.group(1)[:-1]
        if stripped:
            return _normalize_referent(stripped)
    if chinese is not None and chinese.group(1) in {"也", "且"}:
        coordinated = re.search(
            r"([^\s，,;；。.!?！？؟]{1,40})"
            r"(?:无需|不需要)(?:(?:你|您)的?)?(?:批准|授权|验证)"
            r"[，,]\s*(?:也|且)\s*$",
            prefix,
        )
        if coordinated is not None:
            return _normalize_referent(coordinated.group(1))
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
        r"(?:批准|\b(?:approve|approval)\b)", action, re.IGNORECASE
    ):
        return frozenset({_EvidenceScope.APPROVAL})
    if re.search(
        r"(?:授权|\b(?:authorize|authorization)\b)", action, re.IGNORECASE
    ):
        return frozenset({_EvidenceScope.AUTHORIZATION})
    if re.search(
        r"(?:验证|\b(?:verify|verification)\b)", action, re.IGNORECASE
    ):
        return frozenset({_EvidenceScope.VERIFICATION})
    if re.search(
        r"(?:确认|\bconfirm(?:ation)?\b)",
        action,
        re.IGNORECASE,
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
        r"(?:提供|回复|告诉我|需要进一步指令|\binput\b|"
        r"\b(?:provide|reply|let[ \t]+me[ \t]+know|"
        r"waiting[ \t]+for|need[ \t]+your)\b)",
        action,
        re.IGNORECASE,
    ):
        return frozenset({_EvidenceScope.REPLY})
    return frozenset({_EvidenceScope.EXECUTION})


def _resolution_scopes(match: re.Match[str]) -> FrozenSet[_EvidenceScope]:
    if match.lastgroup == "reply":
        return _REPLY_RESOLUTION_SCOPES
    if match.lastgroup == "confirmation":
        return frozenset({_EvidenceScope.CONFIRMATION})
    if match.lastgroup == "choice":
        return frozenset({_EvidenceScope.CHOICE})
    if match.lastgroup == "security_list":
        text = match.group().casefold()
        scopes = set()
        if re.search(r"approval|批准", text):
            scopes.add(_EvidenceScope.APPROVAL)
        if re.search(r"authorization|授权", text):
            scopes.add(_EvidenceScope.AUTHORIZATION)
        if re.search(r"verification|验证", text):
            scopes.add(_EvidenceScope.VERIFICATION)
        return frozenset(scopes)
    if match.lastgroup == "approval":
        return frozenset({_EvidenceScope.APPROVAL})
    if match.lastgroup == "authorization":
        return frozenset({_EvidenceScope.AUTHORIZATION})
    if match.lastgroup == "verification":
        return frozenset({_EvidenceScope.VERIFICATION})
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
            question_actions = clause.question_actions if clause else ()
            if clause and question_actions:
                scoped_referents = tuple(
                    (scope, clause.question_action_referents[action_index])
                    for action_index, (scope, _) in enumerate(
                        question_actions
                    )
                )
            else:
                question_scopes = (
                    clause.question_scopes
                    if clause
                    else frozenset({_EvidenceScope.REPLY})
                )
                scoped_referents = tuple(
                    (
                        scope,
                        _question_referent(question, clause, scope)
                        if clause
                        else None,
                    )
                    for scope in question_scopes
                )
            for scope, referent in scoped_referents:
                evidence.append(
                    _Evidence(
                        block.offset + question.start(),
                        _EvidenceKind.DIRECT_QUESTION,
                        _TEXT_STRENGTH[_EvidenceKind.DIRECT_QUESTION],
                        frozenset({scope}),
                        referent,
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
    excluded = sorted(excluded_spans)
    excluded_starts = [start for start, _ in excluded]

    def is_excluded(start: int, end: int) -> bool:
        index = bisect.bisect_right(excluded_starts, start) - 1
        return (index >= 0 and excluded[index][1] > start) or (
            index + 1 < len(excluded) and excluded[index + 1][0] < end
        )

    primary_matches = list(_REQUIRED_ACTION_RE.finditer(prose))
    clause_index = 0
    covered_primary_index = 0
    for match in _IMPERATIVE_ACTION_RE.finditer(prose):
        if is_excluded(match.start(), match.end()):
            continue
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
        is_choose = re.search(
            r"(?:\b(?:choose|select)\b|选择)",
            match.group(),
            re.IGNORECASE,
        )
        if is_choose is not None:
            has_alternatives = (
                clause is not None
                and (
                    clause.communicative_choice
                    or re.search(
                        r"\b\S+\s+or\s+\S+\b|或", clause.text, re.IGNORECASE
                    )
                    is not None
                )
            )
            if not has_alternatives:
                continue
        prefix_start = max(clause_start, match.start() - 160)
        prefix = prose[prefix_start : match.start()]
        boundaries = list(_IMPERATIVE_BOUNDARY_RE.finditer(prefix))
        if boundaries:
            boundary = boundaries[-1]
            boundary_context = prefix[: boundary.start()]
            action_list = _ACTION_LIST_PREFIX_RE.search(boundary_context) is not None
            if boundary.group().strip() not in {";", "；"}:
                suffix = prose[match.end() : match.end() + 80]
                has_object = _IMPERATIVE_OBJECT_RE.search(suffix) is not None
                report_continuation = (
                    _REPORT_CONTINUATION_RE.search(suffix) is not None
                    or _REPORT_NOUN_SUFFIX_RE.search(suffix) is not None
                )
                if not action_list and (not has_object or report_continuation):
                    continue
            else:
                suffix = prose[match.end() : match.end() + 80]
                if _REPORT_NOUN_SUFFIX_RE.search(suffix) is not None:
                    continue
            prefix = prefix[boundary.end() :]
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
        has_primary_anchor = (
            primary_index >= 0
            and clause is not None
            and primary_starts[primary_index] >= clause.start
        )
        if has_primary_anchor and clause.has_question_mark:
            primary = primary_matches[primary_index]
            primary_start = primary.start() - clause.start
            primary_end = primary.end() - clause.start
            question_action_index = bisect.bisect_right(
                clause.question_action_ends, primary_start
            )
            if (
                question_action_index < len(clause.question_action_ends)
                and clause.question_action_ends[question_action_index]
                <= primary_end
            ):
                has_primary_anchor = False
        has_question_anchor = (
            clause is not None
            and bool(clause.question_actions)
            and clause.question_complement_range is not None
            and clause.question_complement_range[1] < len(clause.text)
            and match.start() - clause.start
            >= clause.question_complement_range[1]
        )
        overlaps_question_action = False
        if has_question_anchor:
            relative_start = match.start() - clause.start
            relative_end = match.end() - clause.start
            action_index = bisect.bisect_right(
                clause.question_action_ends, relative_start
            )
            overlaps_question_action = (
                action_index < len(clause.question_action_ends)
                and clause.question_action_ends[action_index] <= relative_end
            )
        if has_primary_anchor or (
            has_question_anchor and not overlaps_question_action
        ):
            action_matches.append(match)
    clause_index = 0
    for match in _COORDINATED_REQUIREMENT_NOUN_RE.finditer(prose):
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
            and (
                _OWNED_REQUIREMENT_RE.search(primary_matches[primary_index].group())
                is not None
                or re.search(
                    r"\b(?:approval|authorization|verification)\b",
                    primary_matches[primary_index].group(),
                    re.IGNORECASE,
                )
                is not None
            )
        ):
            action_matches.append(match)

    evidence = []
    clause_index = 0
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
            _question_scopes(prose),
            _COMMUNICATIVE_CHOICE_RE.search(prose) is not None,
        )
        relative_start = match.start() - clause.start
        relative_end = match.end() - clause.start
        question_actions = (
            clause.question_actions
            if clause.has_question_mark
            else ()
        )
        if question_actions:
            governing_end = question_actions[0][1]
            overlapping_action = bisect.bisect_right(
                clause.question_action_ends, relative_start
            )
            if (
                overlapping_action < len(clause.question_action_ends)
                and clause.question_action_ends[overlapping_action]
                <= relative_end
            ):
                continue
            if (
                relative_end > governing_end
                and clause.question_complement_range is not None
                and clause.question_complement_range[0]
                < relative_end
                <= clause.question_complement_range[1]
            ):
                continue
        action_suffix = clause.text[relative_end : relative_end + 80]
        if _NON_USER_OWNERSHIP_SUFFIX_RE.search(action_suffix) is not None:
            continue
        context_start = max(0, relative_end - 120)
        action_prefix = clause.text[max(0, relative_start - 120) : relative_start]
        scope_breaks = list(_POLARITY_SCOPE_BREAK_RE.finditer(action_prefix))
        if scope_breaks:
            action_prefix = action_prefix[scope_breaks[-1].end() :]
        not_only_prefix = re.search(r"\bnot[ \t]+$", action_prefix, re.IGNORECASE)
        if not_only_prefix is not None and re.match(
            r"only\b", match.group(), re.IGNORECASE
        ):
            action_prefix = action_prefix[: not_only_prefix.start()]
        action_context = clause.text[context_start:relative_end]
        attribution = None
        for source in _ATTRIBUTED_ACTION_PREFIX_RE.finditer(action_context):
            attribution = source
        attribution_tail = (
            action_context[attribution.end() :]
            if attribution is not None
            else ""
        )
        if (
            (
                attribution is not None
                and (
                    _ATTRIBUTION_SCOPE_BREAK_RE.search(attribution_tail) is None
                    or _ATTRIBUTED_REQUIREMENT_HISTORY_SUFFIX_RE.search(
                        action_suffix
                    )
                    is not None
                )
            )
            or _NEGATED_ACTION_MATCH_RE.search(match.group()) is not None
            or _NEGATED_ACTION_PREFIX_RE.search(action_prefix) is not None
        ):
            continue
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
        quoted_spans = [
            (match.start(), match.end()) for match in _QUOTED_TEXT_RE.finditer(prose)
        ]
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
                _question_scopes(prose),
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
            courtesy_spans
            + quoted_spans
            + list(option_index.body_spans)
            + resolution_spans,
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
