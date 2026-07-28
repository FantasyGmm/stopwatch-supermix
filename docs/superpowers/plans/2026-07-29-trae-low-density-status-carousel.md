# TRAE Low-Density Status Carousel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the dense N/O/P browser mockups with three low-density carousels that show one project, state, or task focus at a time while preserving the approved TRAE upper screen.

**Architecture:** Create a new disposable HTML prototype beside the existing N/O/P comparison so earlier alternatives remain recoverable. The page will share one normalized active-task dataset, while each variant owns a small independent cursor and render function. A single three-second scheduler and the manual advance control will call the same variant-specific step functions, keeping automatic and manual ordering identical.

**Tech Stack:** Self-contained HTML/CSS/JavaScript, existing visual-companion server, in-app browser Playwright checks, Node.js syntax and structural assertions.

---

## File Map

- Create: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`
  - Owns the disposable N/O/P comparison, fake scenarios, carousel state, and interactive controls.
- Reuse unchanged:
  - `.superpowers/brainstorm/5622-1785255196/content/trae-pixel-head.png`
  - `.superpowers/brainstorm/5622-1785255196/content/trae-pixel-idle.png`
  - `.superpowers/brainstorm/5622-1785255196/content/trae-pixel-action.png`
- Reference only: `docs/superpowers/specs/2026-07-29-trae-low-density-status-carousel-design.md`

The prototype does not modify firmware, BLE transport, Mac-agent code, tests,
or production assets.

### Task 1: Establish the New Prototype Boundary

**Files:**
- Create: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`
- Reference: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-nop.html`

- [ ] **Step 1: Copy the approved shell into a new semantic prototype file**

Run:

```bash
cp .superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-nop.html \
  .superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html
```

Expected: the new file exists while the N/O/P predecessor remains unchanged.

- [ ] **Step 2: Run a structural assertion that fails before the redesign**

Run:

```bash
node -e 'const fs=require("fs"),a=require("assert");const h=fs.readFileSync(".superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html","utf8");for(const c of ["n-focus-shell","o-focus-shell","p-focus-shell"])a(h.includes(c),`missing ${c}`)'
```

Expected: FAIL with `missing n-focus-shell`.

- [ ] **Step 3: Rename the page and comparison copy**

Use `apply_patch` so the document starts with these exact user-facing strings:

```html
<title>TRAE 低密度轮播 · N / O / P</title>
<h1>TRAE 下半屏低密度方案</h1>
<p class="lead">一屏只保留一个阅读焦点，通过轮播覆盖全部项目和任务。</p>
```

Change the controls heading to `低密度轮播对比` and explain that N, O, and P
compare project-first, state-first, and task-first information priority.

- [ ] **Step 4: Confirm the existing upper-screen contract remains present**

Run:

```bash
node -e 'const fs=require("fs"),a=require("assert");const h=fs.readFileSync(".superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html","utf8");for(const s of ["brand-icon-slot","BT LIVE","TRAE LIVE","MODEL","Doubao","EFFORT","High","page-dots","battery"])a(h.includes(s),`missing ${s}`);console.log("upper shell preserved")'
```

Expected: `upper shell preserved`.

### Task 2: Replace Dense Lists with One-Focus Layouts

**Files:**
- Modify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Replace the three lower-screen markup blocks**

Use these exact structures:

```html
<section class="variant" id="variantN">
  <button class="n-focus-shell" id="nFocus" aria-label="切换项目任务"></button>
</section>

<section class="variant" id="variantO">
  <button class="o-focus-shell" id="oFocus" aria-label="切换状态任务"></button>
</section>

<section class="variant" id="variantP">
  <button class="p-focus-shell" id="pFocus" aria-label="切换焦点任务"></button>
</section>
```

No task-row, status-column, project-grid, pill, card outline, or rounded lower
container may remain in the active N/O/P markup.

- [ ] **Step 2: Add the N project-first typography**

Implement N with one project and one task:

```css
.n-focus-shell{position:absolute;left:82px;top:235px;width:302px;height:171px;border:0;background:transparent;color:inherit;text-align:left;padding:5px 15px;cursor:pointer}
.n-page{color:#66727e;font-size:10px;letter-spacing:1px}
.n-project{margin-top:8px;color:#9bb1ff;font-size:21px;line-height:26px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.n-state{margin-top:8px;font-size:10px;font-weight:700;letter-spacing:1px}
.n-task{margin-top:5px;color:#edf2f5;font-size:15px;line-height:20px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.n-totals{margin-top:12px;color:#74808c;font-size:10px}
```

- [ ] **Step 3: Add the O state-first typography**

Implement O with a large aggregate state and one task:

```css
.o-focus-shell{position:absolute;left:82px;top:233px;width:302px;height:174px;border:0;background:transparent;color:inherit;text-align:center;padding:4px 17px;cursor:pointer}
.o-count{font-size:34px;line-height:35px;font-weight:750}
.o-label{font-size:12px;line-height:16px;font-weight:700;letter-spacing:2px}
.o-project{margin-top:13px;color:#9bb1ff;font-size:19px;line-height:24px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.o-task{margin-top:3px;color:#edf2f5;font-size:14px;line-height:20px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.o-page{margin-top:8px;color:#687480;font-size:9px}
```

Pending count and label use orange-red; running count and label use cyan. The
count is plain text and must not receive a fill, ring, percentage, or progress
animation.

- [ ] **Step 4: Add the P task-first typography**

Implement P without the previous bottom project-name queue:

```css
.p-focus-shell{position:absolute;left:82px;top:235px;width:302px;height:171px;border:0;background:transparent;color:inherit;text-align:left;padding:13px 20px;cursor:pointer}
.p-state{font-size:11px;font-weight:700;letter-spacing:1.5px}
.p-project{margin-top:14px;color:#9bb1ff;font-size:22px;line-height:27px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.p-task{margin-top:6px;color:#edf2f5;font-size:16px;line-height:21px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.p-page{margin-top:15px;color:#687480;font-size:10px}
```

- [ ] **Step 5: Add a shared empty state**

Use the same low-density empty state in all three buttons:

```html
<span class="focus-empty">暂无活跃任务</span>
```

```css
.focus-empty{display:grid;height:100%;place-items:center;color:#626e7a;font-size:13px;font-weight:600}
```

- [ ] **Step 6: Run the structural assertion again**

Run the command from Task 1, Step 2.

Expected: PASS with exit code 0.

### Task 3: Implement Shared Carousel Ordering and Variant Renderers

**Files:**
- Modify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Define independent cursors and ordering helpers**

Replace the dense-list cursor variables with:

```js
let nProjectIndex=0;
let nTaskIndex=0;
let oStateKey='pending';
let oTaskIndex=0;
let pFocusIndex=0;
let autoPlay=true;
let nextAutoAt=Date.now()+3000;

const activeTasks=()=>{
  const g=groups();
  return [...g.pending,...g.running];
};

function nonEmptyStates(){
  const g=groups();
  return ['pending','running'].filter(state=>g[state].length);
}
```

Reset all cursors and set `oStateKey` to pending when a scenario changes.

- [ ] **Step 2: Render N as one project and one task**

Implement:

```js
function renderN(){
  const projects=projectGroups();
  const host=el('nFocus');
  if(!projects.length){host.innerHTML='<span class="focus-empty">暂无活跃任务</span>';return;}
  nProjectIndex%=projects.length;
  const group=projects[nProjectIndex];
  nTaskIndex%=group.tasks.length;
  const task=group.tasks[nTaskIndex];
  const pc=group.tasks.filter(item=>item.state==='pending').length;
  const rc=group.tasks.filter(item=>item.state==='running').length;
  host.innerHTML='<div class="n-page"></div><div class="n-project"></div><div class="n-state"></div><div class="n-task"></div><div class="n-totals"></div>';
  host.querySelector('.n-page').textContent=`项目 ${nProjectIndex+1} / ${projects.length}`;
  host.querySelector('.n-project').textContent=group.project||'--';
  const state=host.querySelector('.n-state');
  state.className=`n-state ${task.state}`;
  state.textContent=task.state==='pending'?'待确认':'运行中';
  host.querySelector('.n-task').textContent=task.title||'--';
  host.querySelector('.n-totals').textContent=`${pc} 待确认 · ${rc} 运行中`;
}
```

- [ ] **Step 3: Implement O's state-within-task nested carousel**

Implement:

```js
function normalizeO(){
  const states=nonEmptyStates();
  if(!states.length)return null;
  if(!states.includes(oStateKey))oStateKey=states[0];
  const tasks=groups()[oStateKey];
  oTaskIndex%=tasks.length;
  return {states,tasks};
}

function renderO(){
  const host=el('oFocus');
  const current=normalizeO();
  if(!current){host.innerHTML='<span class="focus-empty">暂无活跃任务</span>';return;}
  const task=current.tasks[oTaskIndex];
  host.innerHTML='<div class="o-count"></div><div class="o-label"></div><div class="o-project"></div><div class="o-task"></div><div class="o-page"></div>';
  host.className=`o-focus-shell ${oStateKey}`;
  host.querySelector('.o-count').textContent=current.tasks.length;
  host.querySelector('.o-label').textContent=oStateKey==='pending'?'待确认':'运行中';
  host.querySelector('.o-project').textContent=task.project||'--';
  host.querySelector('.o-task').textContent=task.title||'--';
  host.querySelector('.o-page').textContent=`${oTaskIndex+1} / ${current.tasks.length}`;
}

function advanceO(){
  const current=normalizeO();
  if(!current)return;
  if(oTaskIndex+1<current.tasks.length){oTaskIndex+=1;return;}
  const stateIndex=current.states.indexOf(oStateKey);
  oStateKey=current.states[(stateIndex+1)%current.states.length];
  oTaskIndex=0;
}
```

With the mixed scenario, repeated `advanceO()` calls must produce:

```text
pending 1/2 -> pending 2/2 -> running 1/3 -> running 2/3 -> running 3/3 -> pending 1/2
```

- [ ] **Step 4: Render P as one globally ordered task**

Implement:

```js
function renderP(){
  const tasks=activeTasks();
  const host=el('pFocus');
  if(!tasks.length){host.innerHTML='<span class="focus-empty">暂无活跃任务</span>';return;}
  pFocusIndex%=tasks.length;
  const task=tasks[pFocusIndex];
  host.className=`p-focus-shell ${task.state}`;
  host.innerHTML='<div class="p-state"></div><div class="p-project"></div><div class="p-task"></div><div class="p-page"></div>';
  host.querySelector('.p-state').textContent=task.state==='pending'?'待确认':'运行中';
  host.querySelector('.p-project').textContent=task.project||'--';
  host.querySelector('.p-task').textContent=task.title||'--';
  host.querySelector('.p-page').textContent=`任务 ${pFocusIndex+1} / ${tasks.length}`;
}
```

- [ ] **Step 5: Make automatic and manual advance share the same step functions**

Implement:

```js
function advanceN(){
  const projects=projectGroups();
  if(!projects.length)return;
  const tasks=projects[nProjectIndex%projects.length].tasks;
  if(nTaskIndex+1<tasks.length){nTaskIndex+=1;return;}
  nTaskIndex=0;
  nProjectIndex=(nProjectIndex+1)%projects.length;
}

function advanceP(){
  const tasks=activeTasks();
  if(tasks.length)pFocusIndex=(pFocusIndex+1)%tasks.length;
}

function step(manual=false){
  if(variantKey==='N')advanceN();
  if(variantKey==='O')advanceO();
  if(variantKey==='P')advanceP();
  nextAutoAt=Date.now()+3000;
  renderAll();
}
```

Bind `nFocus`, `oFocus`, `pFocus`, and `nextAction` to `step(true)`. Start the
prototype with automatic demonstration enabled and label the control
`自动演示：开`. Use a lightweight scheduler so a manual selection always gets
a full three seconds before the next automatic advance:

```js
setInterval(()=>{
  if(autoPlay&&Date.now()>=nextAutoAt)step(false);
},250);
```

When the automatic-demonstration control changes from off to on, reset
`nextAutoAt=Date.now()+3000`.

- [ ] **Step 6: Verify JavaScript syntax and required contracts**

Run:

```bash
node -e 'const fs=require("fs"),a=require("assert");const h=fs.readFileSync(".superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html","utf8");const s=h.match(/<script>([\s\S]*?)<\/script>/)[1];new Function(s);for(const name of ["renderN","renderO","renderP","advanceN","advanceO","advanceP"])a(s.includes(`function ${name}`),`missing ${name}`);console.log("low-density script checks ok")'
```

Expected: `low-density script checks ok`.

### Task 4: Browser Verification and Handoff

**Files:**
- Verify: `.superpowers/brainstorm/5622-1785255196/content/trae-layout-compare-low-density.html`

- [ ] **Step 1: Confirm the visual-companion endpoint serves the new file**

Run:

```bash
curl -sS -o /dev/null -w '%{http_code}\n' http://localhost:52202/
```

Expected: `200`.

- [ ] **Step 2: Reload the existing in-app-browser tab at O**

Navigate to:

```text
http://localhost:52202/#variant=O
```

Explicitly reload because the visual-companion server does not reliably hot
reload after file changes.

- [ ] **Step 3: Visually inspect all three variants**

For N, O, and P, take a screenshot and confirm:

- exactly one project/task focus is visible;
- project and task text are larger than the prior comparison;
- no lower-screen card outline, task row, status column, or name queue appears;
- content ends above the page dots and battery;
- the upper-screen TRAE shell is unchanged.

- [ ] **Step 4: Verify O ordering interactively**

Starting from mixed scenario and O, click `执行下一步` four times and read the
visible O focus after each click. Expected sequence:

```text
WebStream / 确认权限变更 / 2 of 2 pending
StopWatch / 构建主干固件 / 1 of 3 running
WebShot / 整理截图任务 / 2 of 3 running
Agent / 读取 Trae 状态 / 3 of 3 running
```

- [ ] **Step 5: Verify scenario edge cases**

Use the existing scenario buttons:

- `只有待确认`: O remains pending while task position advances.
- `只有运行中`: O shows running immediately.
- `没有活跃任务`: all three variants show `暂无活跃任务`.
- Restore `2 确认 / 3 运行` before handoff.

- [ ] **Step 6: Verify health controls and browser logs**

Toggle Bluetooth and process controls once each, confirm their visible labels
change independently, then restore both to live. Read browser warning/error
logs and require an empty result.

- [ ] **Step 7: Leave the browser on recommended variant O**

Keep this deliverable tab visible at:

```text
http://localhost:52202/#variant=O
```

Do not build firmware, flash hardware, stage `.superpowers/`, or include the
prototype in a production commit.
