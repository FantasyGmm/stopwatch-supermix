# Codex Agent / Voice 与任务元数据 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 将 Codex 主页面上半部原位替换为 Agent / Voice，并在每个主页面任务名后显示其精简 Model 与 Effort。

**Architecture:** Mac Agent 在现有任务状态收集阶段按 thread id 附加每任务 Model/Effort；固件以向后兼容方式解析字段并由现有轮播选择当前任务。Agent 名称来自已认证连接与 registry，Voice 状态复用现有状态机；LVGL 只替换既有遥测控件并新增一个任务元数据行组件。

**Tech Stack:** Python 3、SQLite、C++17、cJSON、LVGL、ESP-IDF 5.5.4

**Spec:** `docs/superpowers/specs/2026-08-29-codex-agent-voice-task-metadata-design.md`

## Global Constraints

- 上半部沿用原 Model / Effort 的区域与层级；Agent 长名称和 Voice 圆点/状态按最终实机反馈调整。
- 除明确替换与新增项外，不修改现有 UI 样式。
- 固件必须兼容旧四字段与新六字段任务。
- 状态消息不得超过 2048 字节。
- 不修改安全、BLE UUID、语音生命周期或任务详情协议。

---

### Task 1: Mac Agent 每任务 Model / Effort

**Files:**
- Modify: `mac-codex-agent/codex_adapter.py`
- Modify: `mac-codex-agent/agent.py`
- Test: `mac-codex-agent/tests/test_codex_adapter.py`
- Test: `mac-codex-agent/tests/test_agent.py`

**Interfaces:**
- Produces: `codex_alerts[*].model: str` 与 `codex_alerts[*].effort: str`。
- Consumes: `TaskAlert.thread_id` 和 Codex `threads(id, model, reasoning_effort)`。

- [x] 添加采集器测试：不同 thread id 得到各自 Model/Effort，缺失记录得到空字符串。
- [x] 运行测试并确认因任务字段缺失而失败。
- [x] 最小实现一次 SQLite 查询并把映射附加到 alert。
- [x] 添加紧凑状态测试：保留并限制任务 Model/Effort，且 JSON 小于等于 2048 字节。
- [x] 运行 `test_codex_adapter.py` 与 `_watch_codex_status` 聚焦测试并确认通过。

### Task 2: 固件任务模型兼容解析

**Files:**
- Modify: `main/apps/app_codex_status/codex_status_model.h`
- Modify: `main/apps/app_codex_status/codex_status_model.cpp`
- Modify: `tests/codex_status_model_test.cpp`
- Modify: `tests/codex_alert_carousel_test.cpp`

**Interfaces:**
- Consumes: Task 1 的可选 `model` / `effort` JSON 字段。
- Produces: `CodexTaskAlert::model` 与 `CodexTaskAlert::effort`。

- [x] 添加 C++ 测试：新六字段任务解析并保留元数据，旧四字段任务仍成功。
- [x] 运行 `codex_status_model_test` 并确认新格式失败。
- [x] 最小实现 4/6 字段兼容、类型/UTF-8/长度校验。
- [x] 添加轮播测试，证明当前任务切换时 Model/Effort 跟随 route。
- [x] 运行两个 host 测试并确认通过。

### Task 3: 原位替换 Agent / Voice

**Files:**
- Modify: `main/apps/app_codex_status/codex_status_priority.h`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `tests/codex_status_priority_test.cpp`
- Modify: `mac-codex-agent/tests/test_codex_status_f_layout_contract.py`

**Interfaces:**
- Consumes: 活动 Agent registry 名称、现有连接状态和 `VoiceInputUiState`。
- Produces: 原遥测位置的 Agent / Voice 文本与颜色。

- [x] 添加状态归约测试，覆盖 Agent 名称、CONNECTING/UNAVAILABLE/ERROR 与 Voice 全状态。
- [x] 添加 UI 合同失败断言：标题为 AGENT/VOICE、字体坐标不变、顶部重复圆点控件不存在。
- [x] 运行测试并确认旧实现失败。
- [x] 用原四个遥测坐标和字体创建 Agent/Voice；从活动 fingerprint 查询 registry 名称；移除顶部重复状态行。
- [x] 运行状态和 UI 合同测试并确认通过。

### Task 4: G3 任务同行元数据

**Files:**
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Modify: `mac-codex-agent/tests/test_codex_status_f_layout_contract.py`

**Interfaces:**
- Consumes: 当前 `CodexTaskAlert` 的 title/model/effort。
- Produces: 原 `kAlertDetailY` 上的任务名 → 精简 Model → Effort 徽标。

- [x] 添加布局与源码合同测试：项目坐标保持不变并按确认稿加粗放大，任务同行 Y/字号与新模型、Effort 控件符合最终实机调整。
- [x] 运行测试并确认新控件缺失而失败。
- [x] 创建透明 flex 行；任务标题保持粗体并优先省略；模型删除 `gpt-`、使用 12 px 无框文字；Effort 大写并使用 10 px 细描边徽标；三者按视觉中心线对齐。
- [x] 覆盖无 Model/Effort、长任务名和任务轮播更新路径。
- [x] 运行布局、轮播与合同测试并确认通过。

### Task 5: 验证与交付证据

**Files:**
- Verify only; do not modify unrelated files.

**Interfaces:**
- Consumes: Tasks 1–4。
- Produces: 可构建、可安装的 Agent 源码和固件产物证据。

- [x] 运行聚焦 Python 测试并确认任务元数据、Agent 长名称、Voice 状态与 UI 合同全部通过。
- [x] 运行相关 C++ host 测试与 UI 合同测试。
- [x] 运行 `IDF_PATH=/Users/luke/esp/esp-idf idf.py build` 或项目候选构建流程。
- [x] 运行 `git diff --check`，检查精确 diff 与未跟踪原型。
- [x] 对照 G3 分区复核并刷机；最终字号微调仍以真机 AMOLED 观察为视觉验收依据。
