# Codex Pending Evidence Classifier Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the fixed two-paragraph Codex completion heuristic with a response-wide evidence classifier and reclassify the latest unsuperseded completion after classifier upgrades.

**Architecture:** Keep `completion_classifier.py` as a pure deterministic text classifier, add a small shared rollout-evidence module for both live reduction and upgrade replay, and let `TaskAlertTracker` own ledger migration and notification state. Preserve the BLE payload and watch firmware; an upgraded Mac agent repairs the latest alert locally and uses the existing one-summary-chime path.

**Tech Stack:** Python 3.9, dataclasses, regular expressions, JSONL rollout events, JSON alert ledger, `unittest`, Bash packaging.

---

## Scope and File Map

- Modify `mac-codex-agent/completion_classifier.py`: normalize final responses,
  detect ordered waiting/resolution evidence, and return stable reasons.
- Modify `mac-codex-agent/tests/test_completion_classifier.py`: table-driven
  positive, negative, ordering, language, and exact-regression tests.
- Create `mac-codex-agent/codex_turn_evidence.py`: reduce structured turn
  signals and replay one rollout's latest unsuperseded terminal completion.
- Create `mac-codex-agent/tests/test_codex_turn_evidence.py`: prove structured
  signal reduction and safe replay behavior independently of the ledger.
- Modify `mac-codex-agent/task_alerts.py`: use the shared turn reducer, persist
  `classifier_version`, and reconcile matching latest alerts once per version.
- Modify `mac-codex-agent/tests/test_task_alerts.py`: prove migration,
  supersession, notification, failure preservation, and idempotence.
- Modify `mac-codex-agent/install_launch_agent.sh`: package the new
  `codex_turn_evidence.py` module while preserving all existing local edits.
- Modify `mac-codex-agent/tests/test_install_script.py`: require the new module
  in the bundle.
- Modify `README.md` and `mac-codex-agent/README.md`: document response-wide
  classification, upgrade reconciliation, and unchanged privacy/transport.

Do not modify watch firmware or BLE status fields. The execution workspace
currently contains unrelated unstaged installer fixes; preserve them and stage
only the paths or hunks named by each task.

### Task 1: Detect Direct Questions Across the Whole Response

**Files:**
- Modify: `mac-codex-agent/completion_classifier.py:10-143`
- Modify: `mac-codex-agent/tests/test_completion_classifier.py:11-136`

- [ ] **Step 1: Add the exact missed-response and question-normalization tests**

Add these tests to `CompletionClassifierTests` and update the two existing
question-reason assertions from `text_question` or
`text_question_before_epilogue` to `text_direct_question`:

```python
    def test_question_before_options_and_recommendation_is_waiting(self):
        result = classify_completion(
            "可以共享一个手表主密钥，但仍需独立识别 Agent。\n\n"
            "首次添加新 Agent 时，你希望如何确认？\n\n"
            "1. 手表和 Mac 显示相同验证码。\n"
            "2. 手表开放六十秒配对窗口。\n"
            "3. 只依赖系统蓝牙配对。\n\n"
            "我推荐第 1 种，安全性和可理解性最好。"
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )

    def test_arabic_question_punctuation_is_waiting(self):
        result = classify_completion("هل تريد المتابعة؟\n\nRecommendation follows.")

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_direct_question")

    def test_self_answered_explanation_is_completed(self):
        result = classify_completion(
            "为什么构建失败？原因是缓存中的 Pillow 只有 arm64 架构。\n\n"
            "修复和测试已经完成。"
        )

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "completion_only")

    def test_directed_question_is_not_mistaken_for_self_answer(self):
        result = classify_completion(
            "你希望为什么选择方案 1？原因可以在确认后补充。"
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_direct_question")
```

- [ ] **Step 2: Run the classifier tests and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_completion_classifier.py -v
```

Expected: the exact missed response and Arabic punctuation classify as
Completed, the self-answered explanation classifies as Waiting, and the old
reason assertions fail.

- [ ] **Step 3: Replace the fixed tail window with a response-wide question scan**

Add these constants and helpers beside the existing normalization patterns:

```python
_QUESTION_MARK_RE = re.compile(r"[?？؟¿]")
_ANSWER_CUE_RE = re.compile(
    r"(?:原因(?:是|在于)|答案(?:是|为)|"
    r"\bbecause\b|\bthe answer is\b)",
    re.IGNORECASE,
)
_SECOND_PERSON_RE = re.compile(
    r"(?:你|您|\byou\b|\byour\b)",
    re.IGNORECASE,
)


def _prose(paragraph: str) -> str:
    prose = "\n".join(
        line
        for line in paragraph.splitlines()
        if not _NON_PROSE_LINE_RE.match(line)
    )
    return _QUOTED_QUESTION_RE.sub("", prose)


def _has_unresolved_prose_question(paragraphs: List[str]) -> bool:
    for paragraph in paragraphs:
        prose = _prose(paragraph)
        for question in _QUESTION_MARK_RE.finditer(prose):
            suffix = prose[question.end():]
            self_answered = (
                _ANSWER_CUE_RE.search(suffix) is not None
                and _SECOND_PERSON_RE.search(prose) is None
            )
            if not self_answered:
                return True
    return False
```

Replace the final/tail question checks in `classify_completion()` with:

```python
    final = paragraphs[-1]
    if _has_unresolved_prose_question(paragraphs):
        return CompletionClassification("waiting", "text_direct_question")
```

Keep the existing choice and required-action fallbacks unchanged in this task.
Delete `_has_prose_question()` once no caller remains.

- [ ] **Step 4: Run the focused tests and verify GREEN**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_completion_classifier.py -v
```

Expected: all classifier tests pass.

- [ ] **Step 5: Commit the response-wide question change**

Run:

```bash
git add mac-codex-agent/completion_classifier.py \
  mac-codex-agent/tests/test_completion_classifier.py
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Detect Codex questions across final responses"
```

Expected: one commit containing only the classifier and its tests.

### Task 2: Reduce Ordered Text Evidence

**Files:**
- Modify: `mac-codex-agent/completion_classifier.py`
- Modify: `mac-codex-agent/tests/test_completion_classifier.py`

- [ ] **Step 1: Add table-driven action, choice, resolution, and courtesy tests**

Add the following tests:

```python
    def test_ordered_text_evidence(self):
        cases = (
            (
                "请选择部署环境。",
                ("waiting", "text_required_action"),
            ),
            (
                "Please provide the missing token.",
                ("waiting", "text_required_action"),
            ),
            (
                "如需更多帮助请告诉我。",
                ("completed", "completion_only"),
            ),
            (
                "Let me know if you would like more help.",
                ("completed", "completion_only"),
            ),
            (
                "需要我继续吗？",
                ("waiting", "text_direct_question"),
            ),
            (
                "应该继续吗？原因是缓存仍然过期。无需你回复。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "无需回复，构建已经完成。\n\n你希望部署到哪个环境？",
                ("waiting", "text_direct_question"),
            ),
            (
                "你希望使用 A 还是 B？\n\n已经按 A 实施，不需要再选择。",
                ("completed", "text_explicitly_resolved"),
            ),
        )
        for message, expected in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), expected)

    def test_recommendation_turns_contextual_options_into_handoff(self):
        result = classify_completion(
            "可用方案：\n\n"
            "A. 同码配对\n"
            "B. 限时开放\n"
            "C. 系统配对\n\n"
            "我推荐 A。"
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_choice_handoff"),
        )

    def test_numbered_procedure_is_not_a_choice(self):
        result = classify_completion(
            "安装步骤：\n\n"
            "1. 下载依赖。\n"
            "2. 运行测试。\n"
            "3. 启动应用。\n\n"
            "所有步骤已经执行完成。"
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "completion_only"),
        )
```

Update the existing choice tests to expect `text_choice_handoff`.

- [ ] **Step 2: Run the classifier tests and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_completion_classifier.py -v
```

Expected: courtesy offers, explicit resolution, recommendation-only choice
context, and the new stable choice reason fail under the current classifier.

- [ ] **Step 3: Add ordered evidence primitives**

Add these types and patterns to `completion_classifier.py`:

```python
@dataclasses.dataclass(frozen=True)
class _Evidence:
    offset: int
    kind: str
    strength: int


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
    r"(?:如(?:果)?需(?:要)?更多帮助请告诉我|"
    r"如果你愿意(?:，|,)?请告诉我|"
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
    r"(?:选择|请选择|方案|选项|推荐|"
    r"\bchoose\b|\bselect\b|\boption\b|\brecommend)",
    re.IGNORECASE,
)
_RECOMMEND_LABEL_RE = re.compile(
    r"(?:推荐|recommend)\s*(?:第\s*)?([A-Z1-9])",
    re.IGNORECASE,
)
_OPTION_LINE_RE = re.compile(
    r"^\s*(?:[-*]\s*)?([A-Z]|[1-9])\s*[.、:：)）]\s*\S+",
    re.MULTILINE,
)
_TEXT_STRENGTH = {
    "text_direct_question": 1,
    "text_choice_handoff": 2,
    "text_required_action": 3,
}
```

Use a block value that preserves source order:

```python
@dataclasses.dataclass(frozen=True)
class _Block:
    text: str
    offset: int


def _blocks(message: str) -> List[_Block]:
    cleaned = _strip_non_prose(message)
    blocks = []
    cursor = 0
    for raw in cleaned.split("\n\n"):
        start = cleaned.find(raw, cursor)
        cursor = start + len(raw)
        text = _prose(raw).strip()
        if text and not text.startswith("::"):
            blocks.append(_Block(text=text, offset=start))
    return blocks
```

- [ ] **Step 4: Implement evidence collection and chronological reduction**

Replace `_has_choice_handoff()` and the text portion of
`classify_completion()` with these complete helpers:

```python
def _question_evidence(block: _Block) -> List[_Evidence]:
    result = []
    for match in _QUESTION_MARK_RE.finditer(block.text):
        suffix = block.text[match.end():]
        self_answered = (
            _ANSWER_CUE_RE.search(suffix) is not None
            and _SECOND_PERSON_RE.search(block.text) is None
            and _REQUIRED_ACTION_RE.search(block.text) is None
            and _DECISION_CUE_RE.search(block.text) is None
        )
        if not self_answered:
            result.append(
                _Evidence(
                    block.offset + match.start(),
                    "text_direct_question",
                    _TEXT_STRENGTH["text_direct_question"],
                )
            )
    return result


def _action_evidence(block: _Block) -> List[_Evidence]:
    courtesy_ranges = [
        (match.start(), match.end())
        for match in _COURTESY_RE.finditer(block.text)
    ]
    resolution_ranges = [
        (match.start(), match.end())
        for match in _RESOLUTION_RE.finditer(block.text)
    ]
    result = []
    for match in _REQUIRED_ACTION_RE.finditer(block.text):
        excluded = courtesy_ranges + resolution_ranges
        if any(start <= match.start() < end for start, end in excluded):
            continue
        result.append(
            _Evidence(
                block.offset + match.start(),
                "text_required_action",
                _TEXT_STRENGTH["text_required_action"],
            )
        )
    return result


def _choice_evidence(blocks: List[_Block]) -> List[_Evidence]:
    result = []
    for index, block in enumerate(blocks):
        labels = []
        for match in _OPTION_LINE_RE.finditer(block.text):
            label = match.group(1).upper()
            if label not in labels:
                labels.append(label)
        if len(labels) < 2:
            continue
        neighbors = blocks[max(0, index - 1):index + 2]
        context = "\n\n".join(item.text for item in neighbors)
        recommendation = _RECOMMEND_LABEL_RE.search(context)
        has_named_recommendation = (
            recommendation is not None
            and recommendation.group(1).upper() in labels
        )
        if (
            _QUESTION_MARK_RE.search(context)
            or _DECISION_CUE_RE.search(context)
            or has_named_recommendation
        ):
            result.append(
                _Evidence(
                    block.offset,
                    "text_choice_handoff",
                    _TEXT_STRENGTH["text_choice_handoff"],
                )
            )
    return result


def _text_classification(message: str) -> CompletionClassification:
    blocks = _blocks(message)
    events = []
    for block in blocks:
        events.extend(_question_evidence(block))
        events.extend(_action_evidence(block))
        for match in _RESOLUTION_RE.finditer(block.text):
            events.append(
                _Evidence(
                    block.offset + match.start(),
                    "text_explicitly_resolved",
                    0,
                )
            )
    events.extend(_choice_evidence(blocks))
    events.sort(key=lambda event: (event.offset, event.strength))

    active = []
    saw_resolution = False
    for event in events:
        if event.kind == "text_explicitly_resolved":
            active = []
            saw_resolution = True
        else:
            active.append(event)

    if active:
        strongest = max(active, key=lambda event: event.strength)
        return CompletionClassification("waiting", strongest.kind)
    if saw_resolution:
        return CompletionClassification("completed", "text_explicitly_resolved")
    return CompletionClassification("completed", "completion_only")
```

The final `classify_completion()` body must be:

```python
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
    if not isinstance(message, str) or not message.strip():
        return CompletionClassification("completed", "completion_only")
    return _text_classification(message)
```

Delete the superseded tail-only choice and waiting-phrase functions and
constants. Do not retain two competing classification paths.

- [ ] **Step 5: Run focused and tracker regression tests**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_completion_classifier.py \
  mac-codex-agent/tests/test_task_alerts.py -v
```

Expected: all tests pass. If an existing tracker expectation still names
`text_question` or `text_choice`, update only the expected reason to the new
stable reason; do not weaken outcome assertions.

- [ ] **Step 6: Commit the ordered classifier**

Run:

```bash
git add mac-codex-agent/completion_classifier.py \
  mac-codex-agent/tests/test_completion_classifier.py \
  mac-codex-agent/tests/test_task_alerts.py
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Classify ordered Codex waiting evidence"
```

Expected: one commit with the completed pure classifier and its reason updates.

### Task 3: Share Structured Turn Reduction and Safe Replay

**Files:**
- Create: `mac-codex-agent/codex_turn_evidence.py`
- Create: `mac-codex-agent/tests/test_codex_turn_evidence.py`
- Modify: `mac-codex-agent/task_alerts.py:500-710`

- [ ] **Step 1: Write replay and signal-reducer tests**

Create `mac-codex-agent/tests/test_codex_turn_evidence.py` with these core
tests, reusing small local JSONL append helpers:

```python
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from codex_turn_evidence import (
    RolloutReplayError,
    latest_unsuperseded_completion,
    new_turn_signal,
    reduce_turn_signal,
)


class CodexTurnEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.rollout = pathlib.Path(self.temporary.name) / "rollout.jsonl"
        self.rollout.touch()

    def tearDown(self):
        self.temporary.cleanup()

    def append(self, timestamp, event_type, **payload):
        item = {
            "timestamp": timestamp,
            "type": (
                "response_item"
                if event_type == "response_item"
                else "event_msg"
            ),
            "payload": payload,
        }
        if event_type != "response_item":
            item["payload"]["type"] = event_type
        with self.rollout.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(item) + "\n")

    def test_plan_and_input_calls_share_one_reducer(self):
        signal = new_turn_signal("turn-a")
        plan_call = {
            "type": "function_call",
            "name": "update_plan",
            "arguments": json.dumps({
                "plan": [{"step": "Choose", "status": "in_progress"}]
            }),
            "call_id": "plan-a",
        }
        input_call = {
            "type": "function_call",
            "name": "request_user_input",
            "arguments": "{}",
            "call_id": "input-a",
        }
        input_output = {
            "type": "function_call_output",
            "call_id": "input-a",
        }

        self.assertTrue(reduce_turn_signal(signal, plan_call))
        self.assertTrue(reduce_turn_signal(signal, input_call))
        self.assertTrue(signal["unfinished_plan"])
        self.assertEqual(signal["input_calls"], ["input-a"])
        self.assertTrue(reduce_turn_signal(signal, input_output))
        self.assertEqual(signal["input_calls"], [])

    def test_latest_completion_reconstructs_structured_signals(self):
        with tempfile.TemporaryDirectory() as temporary:
            rollout = pathlib.Path(temporary) / "rollout.jsonl"
            rows = (
                {
                    "timestamp": "2026-07-29T13:00:00Z",
                    "type": "event_msg",
                    "payload": {"type": "task_started", "turn_id": "turn-a"},
                },
                {
                    "timestamp": "2026-07-29T13:00:01Z",
                    "type": "response_item",
                    "payload": {
                        "type": "function_call",
                        "name": "request_user_input",
                        "arguments": "{}",
                        "call_id": "input-a",
                        "internal_chat_message_metadata_passthrough": {
                            "turn_id": "turn-a"
                        },
                    },
                },
                {
                    "timestamp": "2026-07-29T13:00:02Z",
                    "type": "event_msg",
                    "payload": {
                        "type": "task_complete",
                        "turn_id": "turn-a",
                        "last_agent_message": "Choose one.",
                    },
                },
            )
            rollout.write_text(
                "".join(json.dumps(row) + "\n" for row in rows),
                encoding="utf-8",
            )

            result = latest_unsuperseded_completion(rollout)

            self.assertEqual(result.turn_id, "turn-a")
            self.assertTrue(result.unresolved_input)
            self.assertFalse(result.unfinished_plan)

    def test_later_user_message_supersedes_completion(self):
        self.append(
            "2026-07-29T13:00:00Z",
            "task_started",
            turn_id="turn-a",
        )
        self.append(
            "2026-07-29T13:00:01Z",
            "task_complete",
            turn_id="turn-a",
            last_agent_message="Please confirm.",
        )
        self.append("2026-07-29T13:00:02Z", "user_message")

        self.assertIsNone(latest_unsuperseded_completion(self.rollout))

    def test_incomplete_or_invalid_rollout_is_not_replayed(self):
        self.rollout.write_bytes(b'{"timestamp":"2026-07-29T13:00:00Z"')

        with self.assertRaises(RolloutReplayError):
            latest_unsuperseded_completion(self.rollout)
```

- [ ] **Step 2: Run the new tests and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_codex_turn_evidence.py -v
```

Expected: import failure because `codex_turn_evidence.py` does not exist.

- [ ] **Step 3: Implement the shared evidence module**

Create `mac-codex-agent/codex_turn_evidence.py` with this public API:

```python
"""Reduce Codex turn signals and replay the latest terminal completion."""

from __future__ import annotations

import dataclasses
import datetime
import json
import pathlib
from typing import Any, Dict, Optional


UTC = datetime.timezone.utc


class RolloutReplayError(RuntimeError):
    def __init__(self, message: str, *, retryable: bool = False):
        super().__init__(message)
        self.retryable = retryable


@dataclasses.dataclass(frozen=True)
class LatestCompletion:
    timestamp: datetime.datetime
    turn_id: str
    last_agent_message: Any
    unfinished_plan: bool
    unresolved_input: bool


def new_turn_signal(turn_id: str) -> Dict[str, Any]:
    return {
        "turn_id": turn_id,
        "unfinished_plan": False,
        "input_calls": [],
    }


def reduce_turn_signal(signal: Dict[str, Any], payload: Dict[str, Any]) -> bool:
    item_type = payload.get("type")
    if item_type == "function_call" and payload.get("name") == "update_plan":
        arguments = payload.get("arguments")
        if isinstance(arguments, str):
            try:
                arguments = json.loads(arguments)
            except json.JSONDecodeError:
                return False
        if not isinstance(arguments, dict):
            return False
        plan = arguments.get("plan")
        if not isinstance(plan, list):
            return False
        unfinished = any(
            isinstance(item, dict)
            and item.get("status") in ("in_progress", "pending")
            for item in plan
        )
        if signal.get("unfinished_plan") == unfinished:
            return False
        signal["unfinished_plan"] = unfinished
        return True

    if item_type == "function_call" and payload.get("name") == "request_user_input":
        call_id = payload.get("call_id")
        if not isinstance(call_id, str) or not call_id:
            return False
        calls = signal.setdefault("input_calls", [])
        if call_id in calls:
            return False
        calls.append(call_id)
        return True

    if item_type == "function_call_output":
        call_id = payload.get("call_id")
        calls = signal.setdefault("input_calls", [])
        if call_id not in calls:
            return False
        calls.remove(call_id)
        return True
    return False
```

Add a strict timestamp parser and replay function:

```python
def _timestamp(value: Any) -> datetime.datetime:
    if not isinstance(value, str) or not value:
        raise RolloutReplayError("rollout event has no timestamp")
    try:
        parsed = datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise RolloutReplayError("rollout event has invalid timestamp") from error
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)


def latest_unsuperseded_completion(
    path: pathlib.Path,
) -> Optional[LatestCompletion]:
    try:
        data = pathlib.Path(path).read_bytes()
    except OSError as error:
        raise RolloutReplayError("rollout is unreadable") from error
    if data and not data.endswith(b"\n"):
        raise RolloutReplayError(
            "rollout has an incomplete trailing record",
            retryable=True,
        )

    signal = None
    candidate = None
    for raw in data.splitlines():
        try:
            item = json.loads(raw)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise RolloutReplayError("rollout contains an invalid record") from error
        if not isinstance(item, dict) or not isinstance(item.get("payload"), dict):
            continue
        payload = item["payload"]

        if item.get("type") == "response_item":
            metadata = payload.get("internal_chat_message_metadata_passthrough")
            turn_id = metadata.get("turn_id") if isinstance(metadata, dict) else None
            if signal is not None and turn_id == signal.get("turn_id"):
                reduce_turn_signal(signal, payload)
            continue
        if item.get("type") != "event_msg":
            continue

        event_type = payload.get("type")
        if event_type == "task_started":
            turn_id = payload.get("turn_id")
            signal = (
                new_turn_signal(turn_id)
                if isinstance(turn_id, str) and turn_id
                else None
            )
            candidate = None
        elif event_type == "user_message":
            candidate = None
        elif event_type == "task_complete":
            turn_id = payload.get("turn_id")
            matching = (
                signal
                if signal is not None and signal.get("turn_id") == turn_id
                else {}
            )
            candidate = LatestCompletion(
                timestamp=_timestamp(item.get("timestamp") or payload.get("timestamp")),
                turn_id=turn_id if isinstance(turn_id, str) else "",
                last_agent_message=payload.get("last_agent_message"),
                unfinished_plan=bool(matching.get("unfinished_plan", False)),
                unresolved_input=bool(matching.get("input_calls")),
            )
            signal = None
    return candidate
```

- [ ] **Step 4: Make live tracking use the same reducer**

In `task_alerts.py`, import:

```python
from codex_turn_evidence import new_turn_signal, reduce_turn_signal
```

For `task_started`, replace the literal signal dictionary with:

```python
next_signal = new_turn_signal(turn_id)
```

In `_reduce_response_item()`, retain the thread/turn metadata guards, then
replace the local plan/input parsing body with:

```python
        return reduce_turn_signal(signal, payload)
```

Remove the now-unused duplicate JSON plan/input reduction code, but keep the
module's other `json` uses.

- [ ] **Step 5: Run replay, tracker, and classifier tests**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_completion_classifier.py -v
```

Expected: all tests pass, including the pre-existing structured-plan and
resolved-input tracker cases.

- [ ] **Step 6: Commit the shared rollout evidence module**

Run:

```bash
git add mac-codex-agent/codex_turn_evidence.py \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/task_alerts.py
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Share Codex turn evidence replay"
```

Expected: one commit containing the new module, its tests, and the live reducer
deduplication.

### Task 4: Reclassify the Latest Alert Once Per Classifier Version

**Files:**
- Modify: `mac-codex-agent/completion_classifier.py`
- Modify: `mac-codex-agent/task_alerts.py:229-305,385-470,770-833`
- Modify: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Add migration and idempotence tests**

Import `CLASSIFIER_VERSION` in `test_task_alerts.py`, then add tests that first
create a ledger under `CLASSIFIER_VERSION - 1`:

```python
    def test_classifier_upgrade_reclassifies_latest_completion_once(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-29T13:59:00Z",
            turn_id="turn-a",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-29T13:59:06Z",
            turn_id="turn-a",
            last_agent_message=(
                "首次添加 Agent 时，你希望如何确认？\n\n"
                "1. 验证码。\n2. 配对窗口。\n3. 系统配对。\n\n"
                "我推荐第 1 种。"
            ),
        )
        completed = tracker.refresh()
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["alerts"][0].update({
            "state": "completed",
            "outcome": "completed",
            "reason": "completion_only",
            "handled_at": "2026-07-29T13:59:06Z",
            "watch_notified": True,
        })
        saved["classifier_version"] = CLASSIFIER_VERSION - 1
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")

        migrated = self.tracker().refresh()

        self.assertEqual(migrated.pending_count, 1)
        self.assertEqual(migrated.alerts[0].state, "pending")
        self.assertEqual(
            self.tracker().pending_watch_notification_ids(),
            ("thread-a:2026-07-29T13:59:06Z",),
        )
        persisted = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(
            persisted["classifier_version"],
            CLASSIFIER_VERSION,
        )
```

Add this helper to the test class:

```python
    def downgrade_classifier_ledger(self, **alert_updates):
        saved = json.loads(self.ledger.read_text(encoding="utf-8"))
        saved["classifier_version"] = CLASSIFIER_VERSION - 1
        if alert_updates:
            saved["alerts"][-1].update(alert_updates)
        self.ledger.write_text(json.dumps(saved), encoding="utf-8")
```

Add the remaining complete migration tests:

```python
    def test_classifier_upgrade_does_not_resurrect_after_later_user_message(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_started",
            "2026-07-12T00:01:00Z",
            turn_id="turn-a",
        )
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:01:01Z",
            turn_id="turn-a",
            last_agent_message="请选择一个方案。",
        )
        tracker.refresh()
        append_event(self.rollout, "user_message", "2026-07-12T00:01:02Z")
        tracker.refresh()
        self.downgrade_classifier_ledger()

        migrated = self.tracker().refresh()

        self.assertEqual(migrated.pending_count, 0)
        self.assertEqual(migrated.completed_count, 1)
        self.assertEqual(
            json.loads(self.ledger.read_text(encoding="utf-8"))[
                "classifier_version"
            ],
            CLASSIFIER_VERSION,
        )

    def test_classifier_upgrade_preserves_already_notified_pending(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:02:00Z",
            last_agent_message="请确认是否继续？",
        )
        tracker.refresh()
        alert_ids = tracker.pending_watch_notification_ids()
        tracker.mark_watch_notified(alert_ids)
        self.downgrade_classifier_ledger()

        migrated = self.tracker().refresh()

        self.assertEqual(migrated.pending_count, 1)
        self.assertEqual(self.tracker().pending_watch_notification_ids(), ())
        self.assertTrue(migrated.alerts[0].watch_notified)

    def test_classifier_upgrade_is_idempotent_across_restart(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:03:00Z",
            last_agent_message="请选择 A 或 B。",
        )
        tracker.refresh()
        self.downgrade_classifier_ledger(
            state="completed",
            outcome="completed",
            reason="completion_only",
            handled_at="2026-07-12T00:03:00Z",
            watch_notified=True,
        )

        first = self.tracker().refresh()
        first_revision = first.revision
        second = self.tracker().refresh()

        self.assertEqual(first.pending_count, 1)
        self.assertEqual(second.pending_count, 1)
        self.assertEqual(second.revision, first_revision)
        self.assertEqual(
            self.tracker().pending_watch_notification_ids(),
            ("thread-a:2026-07-12T00:03:00Z",),
        )

    def test_classifier_upgrade_retries_after_incomplete_rollout(self):
        tracker = self.tracker()
        tracker.refresh()
        append_event(
            self.rollout,
            "task_complete",
            "2026-07-12T00:04:00Z",
            last_agent_message="请选择 A 或 B。",
        )
        tracker.refresh()
        self.downgrade_classifier_ledger(
            state="completed",
            outcome="completed",
            reason="completion_only",
            handled_at="2026-07-12T00:04:00Z",
            watch_notified=True,
        )
        with self.rollout.open("ab") as stream:
            stream.write(b'{"timestamp":"2026-07-12T00:05:00Z"')

        unchanged = self.tracker().refresh()

        self.assertEqual(unchanged.completed_count, 1)
        waiting_ledger = json.loads(self.ledger.read_text(encoding="utf-8"))
        self.assertEqual(
            waiting_ledger["classifier_version"],
            CLASSIFIER_VERSION,
        )
        self.assertEqual(
            waiting_ledger["classifier_retry_threads"],
            ["thread-a"],
        )

        with self.rollout.open("ab") as stream:
            stream.write(b',"type":"event_msg","payload":{"type":"user_message"}}\n')
        retried = self.tracker().refresh()

        self.assertEqual(retried.pending_count, 0)
        self.assertEqual(
            json.loads(self.ledger.read_text(encoding="utf-8"))[
                "classifier_version"
            ],
            CLASSIFIER_VERSION,
        )
        self.assertEqual(
            json.loads(self.ledger.read_text(encoding="utf-8"))[
                "classifier_retry_threads"
            ],
            [],
        )
```

- [ ] **Step 2: Run tracker tests and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_task_alerts.py -v
```

Expected: failures because the ledger has no classifier version or replay
hook.

- [ ] **Step 3: Add classifier version state**

In `completion_classifier.py`, define:

```python
CLASSIFIER_VERSION = 2
```

In `task_alerts.py`, import it with the replay API:

```python
from codex_turn_evidence import (
    RolloutReplayError,
    latest_unsuperseded_completion,
    new_turn_signal,
    reduce_turn_signal,
)
from completion_classifier import CLASSIFIER_VERSION, classify_completion
```

Add this field to `_initial_state()`:

```python
"classifier_version": CLASSIFIER_VERSION,
"classifier_retry_threads": [],
```

In `_load_state()`, accept a missing or older integer classifier version
without silently changing it:

```python
classifier_version = state.get("classifier_version", 0)
if not isinstance(classifier_version, int) or classifier_version < 0:
    raise ValueError("invalid classifier version")
retry_threads = state.get("classifier_retry_threads", [])
if not isinstance(retry_threads, list) or not all(
    isinstance(thread_id, str) for thread_id in retry_threads
):
    raise ValueError("invalid classifier retry threads")
```

The reconciliation step, not schema migration, owns changing this value.

- [ ] **Step 4: Implement bounded reconciliation**

Add this method to `TaskAlertTracker`:

```python
    def _reconcile_classifier_version(
        self,
        threads: List[_ThreadInfo],
    ) -> bool:
        current = self._state.get("classifier_version")
        retry_threads = set(self._state.get("classifier_retry_threads", []))
        version_changed = current != CLASSIFIER_VERSION
        if not version_changed and not retry_threads:
            return False

        previous_retry_threads = sorted(retry_threads)
        retryable_failures = set()
        changed = False
        latest_alert_by_thread = {}
        for item in self._state["alerts"]:
            timestamp = _parse_timestamp(item.get("progress_at"))
            thread_id = item.get("thread_id")
            if timestamp is None or not isinstance(thread_id, str):
                continue
            existing = latest_alert_by_thread.get(thread_id)
            if existing is None or timestamp >= existing[0]:
                latest_alert_by_thread[thread_id] = (timestamp, item)

        for thread in threads:
            if not version_changed and thread.id not in retry_threads:
                continue
            existing = latest_alert_by_thread.get(thread.id)
            if existing is None:
                continue
            try:
                completion = latest_unsuperseded_completion(thread.rollout_path)
            except RolloutReplayError as error:
                LOGGER.warning(
                    "Unable to reconcile Codex classifier: thread=%s",
                    thread.id,
                    exc_info=True,
                )
                if error.retryable:
                    retryable_failures.add(thread.id)
                continue
            if completion is None:
                continue

            completion_time = _iso(completion.timestamp)
            alert = existing[1]
            if alert.get("id") != "{}:{}".format(thread.id, completion_time):
                continue
            classification = classify_completion(
                completion.last_agent_message,
                unfinished_plan=completion.unfinished_plan,
                unresolved_input=completion.unresolved_input,
            )
            next_state = (
                "pending"
                if classification.outcome == "waiting"
                else "completed"
            )
            previous_state = alert.get("state")
            next_notified = (
                False
                if next_state == "pending" and previous_state != "pending"
                else (
                    bool(alert.get("watch_notified", False))
                    if next_state == "pending"
                    else True
                )
            )
            updates = {
                "state": next_state,
                "outcome": classification.outcome,
                "reason": classification.reason,
                "handled_at": completion_time if next_state == "completed" else None,
                "watch_notified": next_notified,
            }
            if any(alert.get(key) != value for key, value in updates.items()):
                alert.update(updates)
                changed = True

        if current != CLASSIFIER_VERSION:
            self._state["classifier_version"] = CLASSIFIER_VERSION
            changed = True
        next_retry_threads = sorted(retryable_failures)
        if previous_retry_threads != next_retry_threads:
            self._state["classifier_retry_threads"] = next_retry_threads
            changed = True
        if changed:
            self._state["revision"] = int(self._state.get("revision", 0)) + 1
        return changed
```

Call it from `refresh()` after `_scan_threads(threads)` and before
`_sync_thread_metadata(threads)`:

```python
if self._reconcile_classifier_version(threads):
    changed = True
```

This ordering lets new unread events use the new classifier first. The
classifier version is persisted immediately. Only an incomplete trailing JSONL
record is retryable; its thread ID remains in `classifier_retry_threads` until
the next successful replay. Missing or permanently invalid rollouts preserve
their existing state without blocking or repeating reconciliation for other
threads.

- [ ] **Step 5: Run tracker, classifier, agent-chime, and adapter tests**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_completion_classifier.py \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_codex_adapter.py -v
```

Expected: all tests pass. The existing agent test must still prove that several
unnotified Pending IDs produce one chime followed by one acknowledgement call.

- [ ] **Step 6: Commit classifier-version reconciliation**

Run:

```bash
git add mac-codex-agent/completion_classifier.py \
  mac-codex-agent/task_alerts.py \
  mac-codex-agent/tests/test_task_alerts.py
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Reclassify latest Codex alerts on upgrade"
```

Expected: one commit containing only classifier-version persistence and
reconciliation.

### Task 5: Package and Document the New Module

**Files:**
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`
- Modify: `README.md`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Add a failing packaging contract**

In `test_installer_packages_the_menu_application_modules`, add:

```python
"codex_turn_evidence.py",
```

beside `completion_classifier.py` and `task_alerts.py`.

- [ ] **Step 2: Run the packaging test and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_install_script.py -v
```

Expected: failure because the installer does not yet copy
`codex_turn_evidence.py`.

- [ ] **Step 3: Package the module**

In the installer's application-module copy list, add:

```bash
"$ROOT_DIR/codex_turn_evidence.py"
```

next to `completion_classifier.py`. Preserve the existing key-scan and
universal-Pillow edits already present in the working tree; do not rewrite or
stage those hunks as part of this feature unless they have since been committed
separately.

- [ ] **Step 4: Update user and developer documentation**

Replace the fixed completion-classification paragraphs with text that states:

```markdown
The Mac agent classifies the complete final response rather than a fixed
trailing paragraph window. Structured unresolved input and unfinished plans
remain authoritative. Local text evidence recognizes direct questions,
contextual choices, and required actions in source order; only an explicit
later statement that no reply or action remains can clear earlier text
evidence.

The alert ledger stores an independent classifier version. After an upgrade,
the agent replays only each conversation's latest unsuperseded completion,
repairs a changed state, and sends at most one summary chime for newly Pending
conversations. Response content remains local and is never placed in the BLE
payload or alert ledger.
```

Keep the existing descriptions of user replies completing Pending alerts,
Pending-first rotation, and one-summary-chime behavior.

- [ ] **Step 5: Run packaging and documentation-adjacent tests**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_install_script.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_agent.py -v
bash -n mac-codex-agent/install_launch_agent.sh
git diff --check -- README.md mac-codex-agent
```

Expected: all tests pass, Bash syntax is valid, and the diff check is clean.

- [ ] **Step 6: Commit only the feature packaging and documentation hunks**

Stage the new module line and docs. If unrelated installer/test edits are still
unstaged, use `git add -p` and verify the cached diff before committing:

```bash
git add -p mac-codex-agent/install_launch_agent.sh \
  mac-codex-agent/tests/test_install_script.py
git add README.md mac-codex-agent/README.md
git diff --cached --check
git diff --cached --stat
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Package Codex pending evidence replay"
```

Expected: the commit contains the new module packaging contract and
documentation, not the pre-existing installer fixes.

### Task 6: Audit the Local Corpus and Perform End-to-End Acceptance

**Files:**
- No production file changes expected.
- Inspect: `~/.codex/sessions/**/*.jsonl`
- Inspect: `~/.codex/.stopwatch_alerts.json`
- Inspect: `~/Library/Logs/stopwatch-codex-agent.log`
- Install: `~/Applications/StopWatch Codex Agent.app`

- [ ] **Step 1: Create a fully provisioned temporary test environment**

Run:

```bash
STOPWATCH_TEST_ROOT="$(mktemp -d)"
python3 -m venv "$STOPWATCH_TEST_ROOT/venv"
"$STOPWATCH_TEST_ROOT/venv/bin/python" -m pip install --upgrade pip
"$STOPWATCH_TEST_ROOT/venv/bin/python" -m pip install \
  -r mac-codex-agent/requirements.txt
```

Expected: dependency installation succeeds without changing the repository.

- [ ] **Step 2: Run the full Mac-agent suite and syntax checks**

Run:

```bash
"$STOPWATCH_TEST_ROOT/venv/bin/python" -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py' -v
python3 -m py_compile mac-codex-agent/*.py
bash -n mac-codex-agent/install_launch_agent.sh
git diff --check
```

Expected: every Mac-agent test passes; Python and Bash syntax checks and the
repository diff check succeed.

- [ ] **Step 3: Compare the baseline and new classifier on recent local rollouts**

Run this metadata-only audit from the repository root:

```bash
STOPWATCH_BASELINE_DIR="$(mktemp -d)"
git show 0027304:mac-codex-agent/completion_classifier.py \
  > "$STOPWATCH_BASELINE_DIR/completion_classifier.py"
python3 - "$STOPWATCH_BASELINE_DIR/completion_classifier.py" <<'PY'
import importlib.util
import json
import pathlib
import re
import sys

baseline_path = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("baseline_classifier", baseline_path)
baseline = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = baseline
spec.loader.exec_module(baseline)

sys.path.insert(0, str(pathlib.Path("mac-codex-agent").resolve()))
from completion_classifier import classify_completion

rows = []
for path in sorted(pathlib.Path.home().glob(".codex/sessions/**/*.jsonl"))[-100:]:
    matched = re.search(
        r"([0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12})$",
        path.stem,
    )
    thread_id = matched.group(1) if matched else path.stem
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        payload = item.get("payload", {})
        if payload.get("type") != "task_complete":
            continue
        message = payload.get("last_agent_message")
        old = baseline.classify_completion(message)
        new = classify_completion(message)
        if (old.outcome, old.reason) != (new.outcome, new.reason):
            rows.append({
                "thread_id": thread_id,
                "timestamp": item.get("timestamp"),
                "old": [old.outcome, old.reason],
                "new": [new.outcome, new.reason],
            })

print(json.dumps({
    "changed_count": len(rows),
    "changes": rows,
}, ensure_ascii=False, indent=2))
PY
```

Expected: output contains only thread IDs, timestamps, outcomes, and reasons;
it never prints prompt or response content. Confirm the current `功能开发`
completion changes from `completed/completion_only` to a Waiting reason.
Review every other outcome change. If any changed case contradicts the
approved design, stop execution and report the thread ID, timestamp, old/new
classification, and violated design rule without printing response content.

- [ ] **Step 4: Rebuild, install, and restart the Mac agent**

Quit the running app and install:

```bash
osascript -e 'tell application "StopWatch Codex Agent" to quit' || true
./mac-codex-agent/install_launch_agent.sh
open "$HOME/Applications/StopWatch Codex Agent.app"
```

Expected: the installer finishes signing and validation, then the application
starts. If the installer is blocked by unrelated uncommitted packaging work,
finish or integrate that separately rather than weakening the classifier
bundle validation.

- [ ] **Step 5: Verify live migration and one-time notification**

With the watch on the Codex screen, run:

```bash
python3 - <<'PY'
import json
import pathlib

ledger = pathlib.Path.home() / ".codex" / ".stopwatch_alerts.json"
state = json.loads(ledger.read_text(encoding="utf-8"))
target = "019fae12-604c-7c80-a90a-3af247283570"
matches = [
    {
        key: item.get(key)
        for key in (
            "thread_id",
            "title",
            "state",
            "outcome",
            "reason",
            "watch_notified",
        )
    }
    for item in state.get("alerts", [])
    if item.get("thread_id") == target
]
print(json.dumps({
    "classifier_version": state.get("classifier_version"),
    "target": matches[-1:] if matches else [],
}, ensure_ascii=False, indent=2))
PY
tail -100 "$HOME/Library/Logs/stopwatch-codex-agent.log" | \
  rg 'Task alert classified|Bluetooth state|reconcile'
```

Expected:

- the target's latest alert is `pending/waiting`;
- its reason is `text_choice_handoff` or `text_direct_question`;
- the watch displays `等待指令`;
- one summary chime occurs after a successful watch write;
- Bluetooth remains connected.

- [ ] **Step 6: Verify restart idempotence and reply completion**

Restart the app once more:

```bash
osascript -e 'tell application "StopWatch Codex Agent" to quit' || true
open "$HOME/Applications/StopWatch Codex Agent.app"
```

Expected: the alert remains Pending but no second migration chime occurs.

Reply in the `功能开发` conversation, allow the agent to refresh, and inspect
the same ledger fields again. Expected: that conversation becomes Completed,
`reason` becomes `user_replied`, and other conversations are unchanged.

- [ ] **Step 7: Record final evidence**

Run:

```bash
git status --short
git log --oneline -6
```

Expected: only intentionally pre-existing unrelated changes remain unstaged;
all implementation commits are present. Record the focused/full test counts,
corpus audit count, installed bundle result, BLE state, migration result, and
one-time chime result in the implementation handoff.
