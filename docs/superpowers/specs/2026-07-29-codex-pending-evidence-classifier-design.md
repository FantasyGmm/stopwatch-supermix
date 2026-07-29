# Codex Pending Evidence Classifier Design

## Status

Approved design. Implementation has not started.

## Problem

The Mac agent currently decides whether a completed Codex turn is Pending by
checking structured turn signals and then inspecting only the last two prose
paragraphs of the final response. A common decision handoff has more trailing
structure:

1. ask the user a question;
2. list several options;
3. explain a recommendation.

The question is then outside the fixed two-paragraph window. The classifier
returns `completed/completion_only`, the alert ledger records Completed, and
the watch never displays or sounds a Pending alert.

The design must favor avoiding missed Pending alerts over avoiding every false
positive. It must still exclude obvious non-actionable question marks such as
code, URLs, quoted history, headings, and self-answered explanatory questions.

## Goal

Replace the fixed tail-window heuristic with a deterministic, local,
response-wide evidence classifier that:

- recognizes unresolved questions, choices, confirmations, and required user
  actions anywhere in the final response;
- allows only explicit later resolution evidence to clear earlier textual
  waiting evidence;
- keeps structured input requests and unfinished plans authoritative;
- explains every classification with a stable reason;
- reclassifies the latest terminal state after classifier upgrades so an
  already-missed current conversation becomes Pending without waiting for a
  new turn;
- preserves the existing one-summary-chime behavior and does not repeat a
  migration notification after restart.

## Non-Goals

- No watch firmware, BLE schema, carousel, glow, or sound change.
- No network service, LLM call, language model, or response upload.
- No reconstruction or display of historical response content on the watch.
- No attempt to support every natural language. The initial deterministic cue
  vocabulary remains focused on Chinese and English while punctuation-based
  prose questions remain language-independent.
- No wholesale rebuild of the alert history. Upgrade reconciliation is limited
  to each conversation's latest terminal state.

## Classification Interface

`classify_completion()` remains a pure function returning:

```text
CompletionClassification(outcome, reason)
```

The public outcomes remain exactly:

- `waiting`
- `completed`

The stable reasons are:

- `structured_input_request`
- `structured_unfinished_plan`
- `text_required_action`
- `text_choice_handoff`
- `text_direct_question`
- `text_explicitly_resolved`
- `completion_only`

Existing consumers continue mapping `waiting` to `pending` and `completed` to
`completed`. No transport change is required.

## Normalization

The classifier creates an ordered normalized text representation while
retaining source offsets. It removes or ignores:

- memory citations and trailing Codex directives;
- fenced and inline code;
- Markdown link targets and bare URLs;
- Markdown headings and block quotes;
- quoted questions that merely repeat historical text.

The remaining prose is split into ordered semantic blocks. Evidence records
carry their normalized source offset so the reducer can distinguish an action
that was resolved later from a new question that appeared after a resolution.

Invalid, empty, or non-string completion content remains Completed unless a
structured signal requires Pending.

## Evidence Detection

### Structured evidence

An unresolved `request_user_input` call produces
`structured_input_request`. An unfinished plan produces
`structured_unfinished_plan`.

Structured evidence has priority over all textual evidence. Text such as
“无需回复” cannot clear an actually unresolved tool request or unfinished plan.

### Direct questions

A non-noise prose question produces `text_direct_question`. Supported question
punctuation includes the common ASCII, full-width Chinese, Arabic, and inverted
forms where applicable.

A question is treated as self-answered, and therefore non-blocking, only when:

- the question and its answer are in the same semantic block;
- an answer cue such as “原因是”, “答案是”, “because”, or “the answer is”
  follows the question;
- the block has no second-person cue, required-action cue, or choice context.

When the classifier is uncertain, it keeps the question as waiting evidence.

### Required actions

Chinese and English action cues detect confirmation, selection, missing
information, retry, review, and reply requirements even without a question
mark. Examples include “请确认”, “请选择”, “请提供”, “确认后继续”,
“please confirm”, and “please provide”.

Negated resolution forms are detected before positive action cues, so “无需确认”
does not match “确认”. Conditional courtesy offers without a direct question,
such as “如需更多帮助请告诉我” or “let me know if you would like more
help”, are non-blocking. A direct optional question such as “需要我继续吗？”
remains Pending because it explicitly awaits an answer.

### Choice handoffs

A numbered or lettered list is a choice handoff only when it has at least two
unique option labels and nearby decision context. Decision context is any of:

- a preceding or following direct question;
- a selection cue such as “选择”, “方案”, “choose”, or “option”;
- a recommendation that names one of the listed options.

This recognizes “question → long option list → recommendation” without
mistaking installation steps, numbered test procedures, or ordinary reports
for choices.

### Explicit resolution

Only strong, unambiguous phrases create `text_explicitly_resolved`, including:

- no reply, confirmation, selection, or user action is required;
- the previously offered choice has already been applied;
- the earlier question is explicitly stated to be resolved.

Generic completion language such as “构建完成”, “done”, or “I recommend option
1” is not resolution evidence and cannot clear an unresolved handoff.

## Ordered Reduction

Textual evidence is reduced in source order:

1. waiting evidence sets the textual state to Waiting and records its reason;
2. later explicit resolution clears only earlier textual waiting evidence;
3. new waiting evidence after a resolution sets Waiting again;
4. when several unresolved evidence kinds survive, the reported reason uses
   this deterministic strength order:
   `text_required_action`, `text_choice_handoff`, `text_direct_question`;
5. if no textual evidence survives, explicit resolution returns Completed with
   `text_explicitly_resolved`; otherwise the result is
   `completed/completion_only`.

Structured input takes precedence over unfinished plan, and both take
precedence over the complete textual reduction.

Examples:

```text
Question → options → recommendation
=> waiting/text_choice_handoff

Why did this fail? Because the cache was stale. No action is required.
=> completed/text_explicitly_resolved

No reply is required for the completed build. Which deployment should I run?
=> waiting/text_direct_question
```

## Classifier Upgrade Reconciliation

The ledger stores a `classifier_version` independent of its schema version.
Changing classification behavior increments this version.

On the first refresh after a version mismatch, the tracker:

1. examines each known conversation's latest relevant rollout events;
2. selects only the latest terminal `task_complete` when no later
   `user_message` or `task_started` supersedes it;
3. reconstructs that turn's structured signals and reclassifies its final
   response without storing response content in the ledger;
4. updates the existing latest alert only when it represents that same
   completion;
5. marks newly discovered Pending alerts as not yet watch-notified;
6. preserves notification state for alerts that were already Pending;
7. persists the new classifier version even when no alert changes.

Reconciliation is idempotent. Existing pending-notification collection sends
one summary chime for all newly Pending alerts after a successful watch write,
then persists them as notified. Restarting the agent does not replay or chime
again.

If a rollout is missing, truncated, or unreadable, reconciliation preserves
that conversation's existing state and logs a diagnostic message. It does not
rewind the normal cursor or discard other alerts.

## Data Flow

```text
Codex rollout
    |
    +-- structured turn signals
    |
    +-- final response
            |
       normalization
            |
       ordered evidence
            |
       classification + reason
            |
       latest alert ledger
            |
       existing compact BLE snapshot
            |
       watch Pending UI and one summary chime
```

All response inspection remains on the Mac. Only the existing project, title,
state, counts, and notification event reach the watch.

## Testing

### Pure classifier tests

Table-driven tests cover:

- the exact missed “question → three options → recommendation” response;
- a question followed by long risk and operational guidance;
- action requests without question punctuation;
- direct optional questions versus conditional courtesy offers;
- self-answered diagnostic questions;
- explicit resolution before and after waiting evidence;
- a new question after explicit resolution;
- quoted questions, headings, URLs, inline code, and fenced code;
- numbered procedures versus contextual choice lists;
- Chinese, English, and mixed-language responses;
- invalid and empty completion payloads;
- structured input and unfinished-plan priority.

### Tracker integration tests

Integration tests prove:

- a waiting result becomes one Pending alert;
- a later user message completes only that conversation's Pending alert;
- a normal completion remains silent;
- a classifier-version mismatch reclassifies the latest unsuperseded
  completion;
- a later user message prevents an old completion from being resurrected;
- migration produces one notification summary and is idempotent across
  restart;
- unreadable rollout data preserves the existing ledger and cursor.

### Local corpus check

Before release, run both old and new classifiers against final responses from a
bounded recent local rollout corpus. Report only counts, thread identifiers,
old/new outcomes, and reasons; do not print response content. Manually inspect
outcome changes through the local source files when needed.

The expected intentional change includes the current `功能开发` conversation
moving from `completed/completion_only` to Pending.

## Deployment and Acceptance

The Mac agent bundle must be rebuilt, installed, and restarted. Firmware does
not need rebuilding or flashing.

Acceptance requires:

1. the exact current missed response classifies as Waiting;
2. the existing `功能开发` alert becomes Pending on the first upgraded refresh;
3. the connected watch displays `等待指令` and receives one summary chime;
4. restarting the Mac agent does not repeat the chime;
5. replying in that conversation changes it to Completed;
6. all classifier, tracker, agent, installer, and firmware contract tests
   relevant to the unchanged BLE schema pass.
