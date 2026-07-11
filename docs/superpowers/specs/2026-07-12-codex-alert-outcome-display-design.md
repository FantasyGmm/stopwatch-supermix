# Codex Alert Outcome Display Design

## Goal

Make each StopWatch alert answer only three questions:

1. Which project produced the alert?
2. Which Codex conversation produced it?
3. Did Codex finish the requested work, or is it waiting for user input?

The watch must not display the original prompt, task body, response excerpt, or completion time in the Activity area.

## Watch Presentation

A pending alert is rendered as:

```text
PENDING 1/2
StopWatch
等待指令 · 添加蓝牙开关
```

A completed alert is rendered as:

```text
PENDING 2/2
website
已完成 · 修复首页导航
```

When every current-day alert has been acknowledged, the heading remains `ALL HANDLED · N` and uses green. `PENDING` uses red and `NO ALERTS` uses the existing dim gray.

Acknowledgement and outcome remain separate concepts:

- `pending` / `handled` records whether the user has replied in that conversation;
- `completed` / `waiting` records what Codex reported at the end of that turn.

Replying marks prior alerts in the same conversation handled but does not rewrite their recorded outcome.

## Conversation Identity

The project name continues to be the basename of the Codex thread working directory. The conversation name comes from the current `threads.title` value in `~/.codex/state_5.sqlite`.

Codex can replace an initial full user message with a shorter generated title after a turn completes. Each tracker refresh therefore synchronizes the project and title of existing ledger records from the current thread row. Any metadata change increments the alert revision once so both the Mac menu and watch refresh immediately.

Fallbacks remain `--` for a missing project and `Untitled conversation` for a missing title. Prompt text is never used as a watch-side title fallback.

## Outcome Classification

The tracker reads `last_agent_message` from each `task_complete` event and classifies only its final non-empty paragraph. This avoids treating questions quoted earlier in a long response as a request for interaction.

The result is `waiting` when the final paragraph:

- ends with `?` or `？`;
- explicitly asks the user to confirm, choose, reply, provide information, reconnect hardware, or perform another required action;
- states that progress is blocked pending user input.

The matching vocabulary covers concise Chinese and English interaction phrases such as `请确认`, `请回复`, `请提供`, `请选择`, `需要你`, `可以吗`, `let me know`, `please confirm`, `please provide`, and `please choose`.

All other task completions are `completed`. An empty or missing `last_agent_message` also defaults to `completed`. The classifier is a pure function with focused examples for completed work, direct questions, required physical actions, quoted questions, and bilingual prompts.

Existing ledger records without an outcome migrate in memory and on the next write as `completed`; their acknowledgement state and notification history are preserved.

## Mac And BLE Data

`TaskAlert` and the persisted ledger add:

```json
{
  "outcome": "completed"
}
```

The common status payload sends the same value for every alert. The Mac submenu adds `Completed` or `Waiting for Instruction` to each current-day row while retaining its time for desktop history.

The watch payload keeps the existing project/title length limits, eight-record cap, and 2048-byte safety check. Firmware accepts `completed` and `waiting`; a missing value defaults to `completed` for compatibility. An invalid value rejects only the alerts array and preserves the prior carousel.

## Model And Effort Arcs

The two upper arcs become direct values:

- the left arc shows only the model, for example `gpt-5.6-sol`;
- the right arc shows only the uppercased effort, for example `XHIGH`.

The static `MODEL`, `EFFORT`, `auto`, and `reason` captions are removed. Both value labels keep fixed widths and trailing-dot behavior so long values cannot resize the layout. The model uses a smaller font than effort when needed to fit the 92-pixel arc.

## Testing And Deployment

Automated coverage includes:

- completed versus waiting classifier examples;
- title changes propagating into existing persisted alerts;
- old-ledger outcome migration;
- outcome propagation through collector, Mac state, and bounded BLE payload;
- firmware parsing and carousel outcome access;
- green `ALL HANDLED`, red `PENDING`, and gray `NO ALERTS` source contracts;
- direct-value model and effort layout contracts;
- the existing Chinese font contract.

The final verification runs the complete Python suite, ESP-IDF build, partition-size check, launcher-only flash to `ota_0`, boot-log inspection, BLE reconnection, and a real completed/waiting display check. NVS, assets, and the Assistant image in `ota_1` remain unchanged.
