# Pending Classifier Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hand-written natural-language parser with a bounded, high-recall classifier while preserving streaming replay, versioned migration, and one-chime notification behavior.

**Architecture:** `completion_classifier.py` becomes a small ordered evidence reducer with whole-response normalization, direct-question detection, bounded Chinese/English action cues, contextual choices, coarse resolution scopes, and fail-open Pending behavior. `codex_turn_evidence.py` remains the shared streaming replay layer, while `TaskAlertTracker` completes the existing classifier-version migration WIP and retries transient replay failures.

**Tech Stack:** Python 3.9, dataclasses, regular expressions, JSONL rollout replay, `unittest`, Bash packaging.

---

## Scope and Existing Work

The worktree already contains uncommitted Task 4 migration changes in:

- `mac-codex-agent/completion_classifier.py`
- `mac-codex-agent/task_alerts.py`
- `mac-codex-agent/tests/test_completion_classifier.py`
- `mac-codex-agent/tests/test_task_alerts.py`

Preserve the migration behavior and `CLASSIFIER_VERSION = 2`. The classifier
and classifier tests will be deliberately replaced; the tracker WIP remains
unstaged until Task 2 is complete. Do not modify watch firmware or BLE fields.

### Task 1: Replace the Exhaustive Grammar with a Bounded Classifier

**Files:**
- Replace: `mac-codex-agent/completion_classifier.py`
- Replace: `mac-codex-agent/tests/test_completion_classifier.py`

- [ ] **Step 1: Replace wording-level tests with a category contract**

Keep one or two representative cases per category. The new test class must
cover these exact contracts:

```python
from completion_classifier import CLASSIFIER_VERSION, classify_completion


def classify(message, **kwargs):
    result = classify_completion(message, **kwargs)
    return result.outcome, result.reason


class CompletionClassifierTests(unittest.TestCase):
    def test_classifier_version_is_two(self):
        self.assertEqual(CLASSIFIER_VERSION, 2)

    def test_structured_evidence_is_authoritative(self):
        self.assertEqual(
            classify("No reply is required.", unresolved_input=True),
            ("waiting", "structured_input_request"),
        )
        self.assertEqual(
            classify("No reply is required.", unfinished_plan=True),
            ("waiting", "structured_unfinished_plan"),
        )

    def test_original_question_options_recommendation_is_waiting(self):
        message = (
            "首次添加 Agent 时，你希望如何确认？\n\n"
            "1. 验证码。\n2. 配对窗口。\n3. 系统配对。\n\n"
            "我推荐第 1 种。"
        )
        self.assertEqual(classify(message), ("waiting", "text_choice_handoff"))

    def test_questions_are_scanned_across_the_complete_response(self):
        message = "هل تريد المتابعة؟\n\n" + ("Long guidance. " * 200)
        self.assertEqual(classify(message), ("waiting", "text_direct_question"))

    def test_clear_action_requests_are_waiting(self):
        cases = (
            "请确认部署环境。",
            "Please provide the missing token.",
            "Waiting for your reply.",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(classify(message)[0], "waiting")

    def test_non_actionable_content_is_completed(self):
        cases = (
            "Why did it fail? Because the cache was stale.",
            "Opened https://example.test/?variant=A. Done.",
            "Implemented `isReady?()` and all tests passed.",
            "### Ready to merge?\n\nYes, all checks passed.",
            "The log said “continue?”. The repair is complete.",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(classify(message), ("completed", "completion_only"))

    def test_procedure_is_not_a_choice(self):
        message = "安装步骤：\n\n1. 下载。\n2. 测试。\n3. 启动。\n\n全部完成。"
        self.assertEqual(classify(message), ("completed", "completion_only"))

    def test_strong_resolution_is_ordered_and_scope_bounded(self):
        cases = (
            (
                "请确认部署。无需确认。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "无需回复。你希望部署到哪里？",
                ("waiting", "text_direct_question"),
            ),
            (
                "Please confirm deployment. No choice is required.",
                ("waiting", "text_required_action"),
            ),
        )
        for message, expected in cases:
            with self.subTest(message=message):
                self.assertEqual(classify(message), expected)

    def test_classifier_source_stays_bounded(self):
        source = pathlib.Path(__file__).resolve().parents[1] / "completion_classifier.py"
        self.assertLessEqual(len(source.read_text(encoding="utf-8").splitlines()), 600)
```

Also retain compact table cases for inverted punctuation, contextual choices,
courtesy offers, trailing directives, invalid input, and later resolution.
Delete wording-level tests for confirmation tense, voice, subject ownership,
coreference, historical attribution, and adversarial paraphrases.

- [ ] **Step 2: Run the new contract and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_completion_classifier.py -v
```

Expected: the behavior cases pass under the current implementation, while
`test_classifier_source_stays_bounded` fails because the source exceeds 600
lines. This is the refactoring RED gate.

- [ ] **Step 3: Implement the bounded ordered evidence reducer**

Replace the production module with a small implementation containing only:

```python
CLASSIFIER_VERSION = 2


@dataclasses.dataclass(frozen=True)
class CompletionClassification:
    outcome: str
    reason: str


@dataclasses.dataclass(frozen=True)
class _Evidence:
    offset: int
    kind: str
    scope: str
    strength: int
```

The bounded scopes are exactly:

```python
_ALL_SCOPES = frozenset({"reply", "confirmation", "choice", "input", "action"})
```

Use a maximum of these semantic detector groups:

```python
_REQUIRED_ACTION_RE = re.compile(
    r"(?:请(?:确认|批准|选择|提供|回复|复核|重试)|"
    r"等待(?:你的|您的)?(?:回复|确认|选择|输入)|"
    r"需要(?:你|您)(?:确认|批准|选择|提供|回复|操作)|"
    r"\bplease (?:confirm|approve|choose|select|provide|reply|review|retry)\b|"
    r"\bwaiting for your (?:reply|confirmation|approval|choice|input)\b|"
    r"\bneed your (?:confirmation|approval|choice|input|reply)\b)",
    re.IGNORECASE,
)

_RESOLUTION_RE = re.compile(
    r"(?P<all>无需(?:你|您)?(?:回复|操作)|不需要(?:你|您)?(?:回复|操作)|"
    r"\bno (?:reply|user action) is required\b|\bno action remains\b)|"
    r"(?P<confirmation>无需(?:你|您)?(?:确认|批准)|"
    r"不需要(?:你|您)?(?:确认|批准)|"
    r"\bno (?:confirmation|approval) is required\b)|"
    r"(?P<choice>无需(?:你|您)?(?:选择)|不需要(?:你|您)?(?:选择)|"
    r"已经按.+?(?:实施|完成)，?不需要再选择|"
    r"\bno choice is required\b|\bthe choice (?:is|has been) resolved\b)|"
    r"(?P<reply>问题已经解决，?无需回复|"
    r"\bthe question (?:is|has been) resolved\b)",
    re.IGNORECASE,
)
```

Normalize the full response by removing citations, fenced/inline code, link
targets, URLs, headings, block quotes, quoted questions, and trailing `::`
directives. Detect direct question marks response-wide. Treat a question as
self-answered only when an answer cue follows in the same block and no
second-person, action, or decision cue exists.

Map action matches to coarse scopes by bounded keywords:

```python
def _action_scope(text: str) -> str:
    lowered = text.casefold()
    if re.search(r"确认|批准|confirm|approval|approve", lowered):
        return "confirmation"
    if re.search(r"选择|choose|select|choice", lowered):
        return "choice"
    if re.search(r"提供|输入|provide|input", lowered):
        return "input"
    if re.search(r"回复|reply", lowered):
        return "reply"
    return "action"
```

Map resolution capture groups to compatible scopes. An `all` resolution clears
all unresolved text evidence; other resolutions clear only matching scopes.
Questions use `reply`, contextual choices use `choice`, and required actions use
the mapped scope. Reduce evidence in source order and choose the strongest
surviving reason using the existing strength order.

Do not implement tokenization, tense/voice parsing, subject extraction,
ownership, referents, confirmation polarity, historical attribution, or
reversal grammar.

- [ ] **Step 4: Run the category contract and verify GREEN**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_completion_classifier.py -v
python3 -m py_compile mac-codex-agent/completion_classifier.py
git diff --check -- mac-codex-agent/completion_classifier.py \
  mac-codex-agent/tests/test_completion_classifier.py
```

Expected: all tests pass, compilation succeeds, and the classifier is at most
600 lines.

- [ ] **Step 5: Run integration regressions**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_codex_adapter.py -v
```

Expected: all tests pass. If an old wording-level assertion fails, keep it only
when it represents one of the approved semantic categories; otherwise remove
or rewrite it at category level.

- [ ] **Step 6: Commit the convergence refactor**

Stage only the classifier and its tests:

```bash
git add mac-codex-agent/completion_classifier.py \
  mac-codex-agent/tests/test_completion_classifier.py
git diff --cached --check
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Simplify Codex pending classification"
```

### Task 2: Complete Classifier-Version Reconciliation

**Files:**
- Modify: `mac-codex-agent/codex_turn_evidence.py`
- Modify: `mac-codex-agent/task_alerts.py`
- Modify: `mac-codex-agent/tests/test_codex_turn_evidence.py`
- Modify: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Preserve the existing migration WIP tests**

Keep tests for exact missed-response migration, later-user-message
supersession, already-notified Pending preservation, restart idempotence,
incomplete-tail retry, and missing/invalid rollout state preservation.

Add this transient unreadable replay contract:

```python
def test_unreadable_rollout_error_is_retryable(self):
    with mock.patch.object(pathlib.Path, "open", side_effect=OSError("busy")):
        with self.assertRaises(RolloutReplayError) as caught:
            latest_unsuperseded_completion(self.rollout)
    self.assertTrue(caught.exception.retryable)
```

- [ ] **Step 2: Run the replay test and verify RED**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py -v
```

Expected: the unreadable-rollout assertion fails because an opening `OSError`
is currently non-retryable; existing migration tests pass.

- [ ] **Step 3: Make transient unreadable rollout replay retryable**

Change the open error to:

```python
except OSError as error:
    raise RolloutReplayError("rollout is unreadable", retryable=True) from error
```

Keep a complete malformed JSON record non-retryable. Keep incomplete trailing
JSON retryable. In `TaskAlertTracker._reconcile_classifier_version()`, preserve
the existing alert on every replay error and retain only retryable thread IDs in
`classifier_retry_threads`.

Ensure the WIP still:

- stores `classifier_version` and `classifier_retry_threads`;
- reclassifies only a matching latest alert;
- never resurrects a completion after a later user message or task start;
- sets newly Pending alerts to `watch_notified=False`;
- preserves notification state for already Pending alerts;
- advances the global classifier version while retrying only affected threads;
- remains revision-idempotent after success.

- [ ] **Step 4: Run migration and chime tests**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_completion_classifier.py \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_codex_adapter.py -v
```

Expected: all tests pass, including one-summary-chime behavior.

- [ ] **Step 5: Commit the migration**

```bash
git add mac-codex-agent/codex_turn_evidence.py \
  mac-codex-agent/task_alerts.py \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py
git diff --cached --check
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Reclassify latest Codex alerts on upgrade"
```

### Task 3: Package and Document the Shared Replay

**Files:**
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`
- Modify: `README.md`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Add the packaging RED**

Add `"codex_turn_evidence.py"` to the installer module contract beside
`completion_classifier.py`, then run:

```bash
python3 -m unittest mac-codex-agent/tests/test_install_script.py -v
```

Expected: failure because the installer does not yet copy the module.

- [ ] **Step 2: Package the module**

Add this source path to the application module copy list:

```bash
"$ROOT_DIR/codex_turn_evidence.py"
```

Do not alter unrelated installer behavior.

- [ ] **Step 3: Update documentation with the bounded behavior**

Document these exact guarantees:

```markdown
The Mac agent scans the complete final response. Structured input requests and
unfinished plans remain authoritative. A bounded local classifier recognizes
direct questions, common Chinese/English action requests, and contextual
choices. It deliberately does not attempt full natural-language parsing;
ambiguous handoffs remain Pending and only a small set of explicit later
resolution phrases can clear earlier text evidence.

The alert ledger stores an independent classifier version. After an upgrade,
the agent streams each conversation's latest rollout, repairs only its latest
unsuperseded completion, and sends at most one summary chime for newly Pending
conversations. Response content remains local and is never written to the BLE
payload or alert ledger.
```

- [ ] **Step 4: Verify and commit packaging/docs**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_install_script.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_agent.py -v
bash -n mac-codex-agent/install_launch_agent.sh
git diff --check -- README.md mac-codex-agent
```

Then commit only these paths:

```bash
git add README.md mac-codex-agent/README.md \
  mac-codex-agent/install_launch_agent.sh \
  mac-codex-agent/tests/test_install_script.py
git -c user.name='luke' -c user.email='lukeluccc@gmail.com' \
  commit -m "Package bounded Codex pending replay"
```

### Task 4: Bounded Corpus and End-to-End Acceptance

**Files:**
- No production source changes expected.

- [ ] **Step 1: Run all locally available Mac-agent tests**

Run the documented test discovery command in the existing configured test
environment. Record passing counts and separately record unavailable optional
macOS, Pillow, cryptography, or LVGL dependencies; do not treat unchanged
environment collection failures as feature regressions.

- [ ] **Step 2: Audit a bounded recent local corpus**

Compare the pre-feature classifier at `0381b89` with current HEAD over a bounded
recent set of final responses. Output only thread ID, old/new outcome, and
reason; never print response text. Group changes into:

- intended handoff detection;
- acceptable conservative Pending;
- suspicious completion noise.

Do not add wording rules from this audit. A new rule requires a distinct,
high-frequency semantic category and a separately approved design.

- [ ] **Step 3: Install and restart the Mac agent**

Run the repository's documented installer, verify the LaunchAgent is loaded,
and confirm logs show no import or ledger errors.

- [ ] **Step 4: Verify the original live regression and notification idempotence**

Verify that the latest unsuperseded `功能开发` completion becomes Pending, the
watch receives one summary chime after a successful write, and restarting the
agent does not repeat it. After a user reply, verify the conversation becomes
Completed.

- [ ] **Step 5: Run final verification**

Run focused tests once more, `py_compile`, `bash -n`, `git diff --check`, verify
`completion_classifier.py` is at most 600 lines, and confirm the worktree has
no unintended changes.

