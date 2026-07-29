# TRAE Round Screen Anchor Layout Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the failed lower-right filler layouts with three round-screen compositions built only from global status counts, current project, and current task.

**Architecture:** Keep the existing throwaway HTML prototype and its task scenarios. Add three rendering-only variants that share data but use independent layout structures; the currently highlighted count identifies the rotating task state without repeating the state label in the task detail.

**Tech Stack:** Plain HTML, CSS, and JavaScript; local static prototype server; in-app browser verification.

---

### Task 1: Add three anchor-based layouts

**Files:**
- Modify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Replace the variant switcher with X, Y, and Z**

```html
<button data-variant="X">双锚居中</button>
<button data-variant="Y">低位双角</button>
<button data-variant="Z">动态主锚</button>
```

- [ ] **Step 2: Add independent CSS layouts**

```css
.x-shell { /* equal left and right state anchors; centered task */ }
.y-shell { /* current task above two lower-corner state anchors */ }
.z-shell { /* active state count expands while the inactive anchor recedes */ }
```

- [ ] **Step 3: Keep the information set minimal**

Each variant renders only `pending count`, `running count`, `project`, and `task`. Do not render queue position, project workload, next task, or a second state label.

### Task 2: Wire the carousel state

**Files:**
- Modify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Add one cursor per variant**

```js
let xFocusIndex = 0;
let yFocusIndex = 0;
let zFocusIndex = 0;
```

- [ ] **Step 2: Highlight only the matching global count**

```js
summary.className = `anchor-summary ${task.state}`;
```

- [ ] **Step 3: Preserve existing interactions**

Clicking the task area, pressing the next control, and automatic playback advance the selected variant. Empty scenarios show both counts as zero and `暂无活跃任务`.

### Task 3: Verify the visual result

**Files:**
- Verify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Parse the inline script**

Run: `node -e 'new Function(extractedScript)'`

Expected: no syntax error.

- [ ] **Step 2: Inspect X, Y, and Z in the browser**

Expected: no visible element crosses the watch bounds or page-dot boundary, and the watch contains exactly one `待确认` and one `运行中` label.

- [ ] **Step 3: Verify empty state and carousel**

Expected: all three variants render the empty state, and one manual advance changes the displayed project or task.

### Task 4: Raise information density with a real next-task preview

**Files:**
- Modify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Add a subordinate preview line to each anchor layout**

```html
<div class="anchor-next">
  <small>下一任务</small>
  <span>WebStream · 确认权限变更</span>
</div>
```

The preview uses the next item from the same pending-first active queue. It is smaller than the current task and must not include queue position or repeat the current task's state label.

- [ ] **Step 2: Increase the primary task scale and vertical span**

```css
.anchor-current-project { font-size: 23px; }
.anchor-current-task { font-size: 16px; }
.anchor-next { font-size: 10px; }
```

Keep the two global state counts as the only numeric status data. Reposition the counts and task content independently in X, Y, and Z so each variant remains structurally distinct.

- [ ] **Step 3: Verify medium density**

Expected: the active variant contains one current project/task and one next project/task, no text overflows, all content remains above the page dots, and `待确认` / `运行中` each appear once.

### Task 5: Move Y statistics above the task carousel

**Files:**
- Modify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Reorder Y vertically with CSS only**

```css
.y-stat-left,
.y-stat-right { top: 8px; bottom: auto; }
.y-project { top: 58px; }
.y-task { top: 88px; }
.y-next { top: 124px; }
```

Keep the existing DOM and data flow. The resulting reading order must be global status counts, current project/task, then next-task preview.

- [ ] **Step 2: Verify the adjusted Y layout**

Expected: the statistics are visually above both task layers, no element crosses the page dots, and task click/automatic playback still advances the current and next task together.
