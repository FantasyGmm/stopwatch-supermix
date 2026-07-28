# TRAE Low-Density Status Carousel Design

## Status

Approved interaction direction for the disposable browser prototype. This
refines the existing N, O, and P lower-screen explorations; it does not yet
change firmware, BLE transport, the Mac agent, or the established upper-screen
TRAE layout.

## Problem

The current borderless variants still try to show every active project and
task at once. On a 466 x 466 circular display, this forces project and task
names to become too small. Removing card borders alone does not solve the
information-density problem.

The revised principle is:

> One screen, one focus. Aggregates explain how much work exists; rotation
> reveals the individual work without shrinking it into a list.

## Shared Constraints

- Preserve the approved TRAE header, independent Bluetooth and process states,
  robot animation, model, effort, divider, page dots, and battery position.
- Do not add token percentages, quota progress, or a usage progress bar.
- Include only `pending` and `running` tasks. Completed tasks remain absent.
- Pending tasks retain higher display priority than running tasks.
- Project and task names must be materially larger than in the current N/O/P
  prototype and must use the available lower-screen width.
- Long names use a single-line ellipsis. Missing names render as `--`; the UI
  never invents values.
- A state or project with no tasks is skipped rather than shown as an empty
  carousel page. With no active tasks, show a centered `暂无活跃任务` state.

## Variant N: Project-First Carousel

N shows one active project at a time.

The lower screen contains:

1. project position, such as `1 / 4`;
2. large project name;
3. the current task state;
4. large task name;
5. compact project totals, such as `1 待确认 · 1 运行中`.

Projects rotate in stable order. Within a project, pending tasks appear before
running tasks. A tap advances to the next task in the project; after the last
task, it advances to the next project.

This variant answers: **Which project needs attention?**

## Variant O: State-First Nested Carousel

O is the recommended direction. It shows one state and one task at a time.

For the mixed example, the first page is:

```text
2
待确认

StopWatch
确认固件刷写
1 / 2
```

The second pending task keeps the large `2 待确认` heading and changes only
the project, task, and position. After all pending tasks have appeared once,
the carousel changes to the running group:

```text
3
运行中

StopWatch
构建主干固件
1 / 3
```

Automatic behavior:

- each task remains visible for three seconds;
- all tasks in the current state appear once before the state changes;
- pending is the initial state whenever pending tasks exist;
- when one state is empty, only the non-empty state rotates;
- when both states are empty, the carousel stops on the empty state.

Manual behavior:

- tapping the lower-screen focus advances one task immediately;
- advancing past the final task switches to the next non-empty state;
- automatic rotation resumes three seconds after the manual selection;
- a horizontal page swipe remains reserved for Codex/TRAE page navigation.

This variant answers: **What kind of attention is needed now?**

## Variant P: Task-First Focus

P shows only the single highest-priority active task. It removes the bottom
project-name queue from the current prototype.

The lower screen contains:

1. large localized state label;
2. large project name;
3. large task name;
4. a small position indicator such as `1 / 5`.

Tasks rotate in pending-first stable order. A tap advances immediately. No
other project or task names share the screen.

This variant answers: **What is the next task I should look at?**

## Visual Hierarchy

The lower screen uses no card outline, rounded task container, status columns,
or multi-row task list. Hierarchy comes from:

- large count or project name;
- state color: orange-red for pending, cyan for running;
- one large task name;
- one restrained position indicator;
- at most one thin separator inherited from the approved shell.

The state count is an aggregate, not a progress value. No fill animation or
percentage treatment may make it resemble usage progress.

## Prototype Verification

The updated interactive prototype must verify:

- N, O, and P at the mixed `2 pending / 3 running` scenario;
- readable project and task names without overlapping the page dots;
- O rotates both tasks within pending before switching to running;
- manual advance follows the same ordering as automatic rotation;
- pending-only, running-only, and no-active-task scenarios;
- independent Bluetooth and TRAE-process controls remain functional;
- the browser console contains no warnings or errors.

Firmware implementation, build, flashing, BLE delivery, and physical watch
acceptance are outside this prototype revision.
