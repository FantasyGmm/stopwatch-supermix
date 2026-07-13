# Unified Assistant 影子迁移设计

**日期：** 2026-07-13

**状态：** 已确认

**适用范围：** StopWatch 统一 Assistant 固件在手表暂不可用期间的离线开发、候选构建和后续分阶段硬件验证

## 1. 目的

本设计补充《StopWatch 统一运行时与 Assistant 合并设计》，不改变其最终架构和验收标准。它调整交付顺序：先完成能够离线实现和自动验证的运行时、界面、诊断、单分区候选及刷写工具，再在手表可用后按阶段完成硬件验证和必要修正。

硬件测试通过前，仓库必须保留旧双 OTA 固件的恢复能力。未经硬件验证的功能只能标记为 build-verified 或 host-tested，不能标记为 hardware-passed。

## 2. 固定边界

- 允许提前实现 Assistant 状态机、运行时、Mooncake UI、诊断以及统一分区候选工具。
- 允许生成能够完整通过容量检查的 unified 候选镜像。
- 保留当前 `partitions.csv`、旧刷写工具和恢复路径，直到完整硬件回归通过。
- 不在手表不可用期间刷写固件、修改设备分区、擦除 NVS 或删除旧双 OTA 工具。
- 不因离线测试通过而宣称麦克风、扬声器、Wi-Fi、BLE 共存、Xiaozhi 兼容性或资源释放已经通过硬件验证。
- 最终产品仍采用一个应用分区；影子迁移不是长期保留两套发布方案。

本设计覆盖原实施计划的 Tasks 8、9、11，以及 Task 10 中可提前完成的候选分区、构建、容量检查、备份、刷写和恢复工具。硬件操作、旧路径删除和正式切换继续延期。

## 3. 双路径仓库布局

### 3.1 现有恢复路径

当前双 OTA 分区和相关工具保持可用，用于恢复已知可工作的旧版本。HEAD 上的新统一运行时代码不要求继续适配旧 OTA 槽容量；恢复应使用已知旧版本、明确的提交或测试前读取的完整 Flash 备份，而不是把超出旧槽容量的新固件强行写入旧布局。

### 3.2 Unified 候选路径

新增独立候选路径：

- `partitions_unified.csv` 定义单个 7.5 MiB factory 应用分区、1 MiB storage、4 MiB assets 和 coredump。
- unified 专用 sdkconfig defaults 选择候选分区表，不覆盖默认 `sdkconfig`。
- 候选构建使用独立 `build-unified` 目录，避免污染现有构建产物。
- 候选构建脚本执行 clean configure、完整编译、assets 生成、镜像结构检查、容量门禁和 SHA256 输出。
- 候选完整刷写脚本只写明确的 bootloader、partition table、application 和 assets 地址，不执行隐式 `erase_flash`。
- Flash 备份脚本在首次候选刷写前读取完整 16 MiB Flash，并生成 SHA256。
- Flash 恢复脚本只能接受尺寸和 SHA256 校验均通过的完整备份。

统一候选应用不得超过 `0x660000` 字节。该上限为 `0x780000` 应用分区保留至少 15% 余量。`generated_assets.bin` 必须非空、校验和正确且不超过 `0x400000` 字节。

### 3.3 正式切换

只有六阶段验证全部通过后，才执行以下清理：

1. 将 unified 分区表设为默认；
2. 删除 Launcher/Assistant 分区切换代码；
3. 删除已确认无调用者的双 OTA 工具；
4. 更新 README 为单固件完整刷写流程；
5. 生成最终发布候选和验证报告。

## 4. 运行时架构

```text
Mooncake AppAssistant
        ↓
RuntimeSupervisor
        ↓
AssistantRuntime
        ↓
Brookesia Agent Manager / Xiaozhi / Audio
        ↓
StopWatch HAL
```

- Mooncake 只管理前台界面和用户操作，不直接调用 Xiaozhi 服务。
- `RuntimeSupervisor` 管理 Assistant、Wi-Fi、录音和播放资源的所有权。
- `AssistantRuntime` 管理 Xiaozhi 启动、停止、回调订阅和停止确认。
- Brookesia 管理 Agent 与服务生命周期。
- StopWatch HAL 继续作为显示、总线、codec、I2S、按钮、BLE 和 Wi-Fi 板级能力的唯一硬件所有者。
- Diagnostics 被动采集会话前、运行中和停止后的快照，不增加常驻高频轮询任务。

现有兼容性 Probe 改为 `AssistantRuntime` 上的调试外壳。Probe 和正式 Mooncake Assistant 必须经过同一生命周期实现，避免硬件验证覆盖一套临时代码、产品运行另一套代码。

## 5. Assistant 生命周期

### 5.1 状态

```text
Stopped → Starting → Running → Stopping → Stopped
Stopped → Starting → Failed → Stopping → Stopped
Running → Stopping → StopTimedOut
StopTimedOut → Stopped 仅在收到真实 Stopped 事件后
```

每次启动生成单调递增的 `session_id`。所有异步回调携带或绑定该标识；不属于当前会话的迟到回调必须被忽略。

### 5.2 启动顺序

`AssistantRuntime` 按以下顺序申请资源：

1. Wi-Fi STA lease；
2. capture 所有权；
3. playback 所有权；
4. Brookesia Audio service binding；
5. Agent Manager 和 Xiaozhi binding；
6. `Activate`；
7. 收到 `Activated` 后异步执行 `Start`。

用户在 `Starting` 期间返回时，会话立即进入停止流程。后续到达的激活回调不得再次触发 `Start`。

### 5.3 停止顺序

停止请求必须幂等，并立即禁止新输入。运行时只提交一次 Agent `Stop`，不阻塞 Mooncake UI，也不使用外部 `vTaskDelete()`。收到当前会话的真实 `Stopped` 事件后，按以下顺序释放：

1. 断开 Agent 事件订阅并释放 Agent binding；
2. 释放 Audio service binding；
3. 释放 playback；
4. 释放 capture；
5. 最后释放 Wi-Fi lease。

正常停止目标为 2 秒。超时后进入 `StopTimedOut`，保留资源冲突保护并记录未退出任务、binding、订阅和 owner。再次停止请求可以重试，但界面不能假装会话已经结束。迟到的真实 `Stopped` 仍可完成正常清理并返回 Launcher。

## 6. 事件和 UI

Brookesia 回调只向有界事件队列写入数据，不直接操作 LVGL。Mooncake `onRunning()` 在 UI 线程消费事件并展示：

- 正在连接 Wi-Fi；
- 正在激活和激活码；
- 正在聆听；
- 正在思考；
- 正在回答；
- 用户文字、Assistant 文字和 emotion；
- 连接失败和重试；
- 正在关闭；
- 关闭超时和再次停止。

生命周期、错误和停止确认事件不可丢弃。文本与 emotion 展示事件在队列满时可以丢弃最旧项，但必须增加丢弃计数供诊断读取。

正常返回操作触发异步停止。收到 `Stopped` 后才能销毁 Assistant LVGL 对象并关闭 Mooncake 应用。关闭期间忽略新的会话操作和重复返回。Wi-Fi、资源申请、激活或协议失败时，UI 显示具体失败阶段；运行时清理已获得资源后允许重试。

## 7. 离线可完成交付

手表不可用期间完成以下内容：

1. 纯 C++ Assistant 状态机和宿主机测试；
2. `AssistantRuntime`、会话标识、事件队列、启动和停止编排；
3. Mooncake 原生 Assistant UI 和异步关闭流程；
4. 资源、heap、PSRAM、任务、binding、订阅和停止耗时诊断；
5. unified 候选分区、独立构建、完整 Flash 备份、候选刷写和恢复工具；
6. Python 源码契约、依赖锁检查、assets 检查和容量门禁；
7. clean unified 候选构建及机器可读指标报告；
8. 分阶段硬件测试清单和结果报告结构。

离线完成的提交必须明确区分 automated pass、build-verified 和 pending hardware。

## 8. 六阶段验证

### 阶段 1：离线自动验证

- 运行全部 Python 契约测试和仓库定义的宿主机 C++ 测试。
- clean 构建 unified 候选。
- 检查只有一个 `app_main()`，产品路径不再调用分区切换。
- 检查应用和 assets 容量、结构、校验和及 SHA256。
- 输出锁定依赖版本和机器可读容量报告。

### 阶段 2：备份与基础刷机

- 读取完整 16 MiB Flash，验证备份尺寸并记录 SHA256。
- 不执行 `erase_flash`，保留原 NVS 和 Xiaozhi 身份。
- 完整写入候选 partition table、application 和 assets。
- 验证启动、Launcher、显示、按钮、启动音效和基础内存状态。
- 失败时使用经过校验的完整 Flash 备份恢复。

### 阶段 3：Xiaozhi 兼容性门禁

- 验证现有身份或经批准的重新激活、激活码、本地唤醒词和手动开始。
- 验证用户文本、Assistant 文本、emotion、TTS 和现有 Xiaozhi/MCP 产品能力。
- 将每项证据写入兼容性报告。所有必选项通过前，兼容性门禁保持 Pending 或 Failed。

### 阶段 4：生命周期与共存

- 分别在启动、聆听、录音、播放和重连时退出。
- 验证正常停止不超过 2 秒，并验证第二次进入。
- 退出后立即验证 Clock、Codex、Badge、BLE、Wi-Fi 和配置 AP。
- 先执行至少 10 次有针对性的开关循环，再进入压力阶段。

### 阶段 5：压力与泄漏

- 完成 100 次 Assistant 打开和关闭。
- 无崩溃、看门狗和意外重启。
- 每次停止后的内部 heap 与对应会话前相比损失不超过 4 KiB。
- 100 次累计内部 heap 损失不超过 8 KiB。
- Agent、Audio 和激活任务归零；事件订阅归零；capture、playback 和 Assistant Wi-Fi owner 全部释放。

### 阶段 6：完整回归与正式切换

- 回归 Launcher、Clock、Codex、Badge、IMU、FFT、Setup、BLE、按钮和电源行为。
- 验证 BLE 状态链路与 Assistant Wi-Fi/音频共存。
- 固化最终 Flash、heap、PSRAM、任务数和停止耗时报告。
- 所有门禁通过后执行第 3.3 节的正式切换和旧路径清理。

## 9. 失败和修正策略

任一阶段失败时停止后续阶段，并记录：

- firmware Git SHA 和镜像 SHA256；
- 锁定组件版本；
- 设备、端口、分区布局和 reset reason；
- 串口日志、复现步骤、运行时状态和资源快照；
- 期望结果与实际结果。

修复后重跑当前阶段以及所有受修改影响的前置阶段。硬件失败不得通过放宽停止、泄漏、容量或兼容性标准来关闭。无法安全继续时使用完整 Flash 备份恢复旧版本。

## 10. 完成定义

只有满足以下条件，统一 Assistant 迁移才算完成：

- 离线交付项全部实现并通过自动验证；
- 六阶段硬件验证全部通过；
- Assistant 打开和退出均不重启；
- 正常停止在 2 秒内收到真实确认并释放全部会话资源；
- 100 次压力测试满足 heap、任务和 owner 门禁；
- 现有应用和 BLE/Wi-Fi 共存回归通过；
- unified 应用保持至少 15% 分区余量；
- 单分区完整刷写和完整 Flash 恢复均验证成功；
- 默认配置切换到 unified，旧双 OTA 产品路径完成受控删除；
- 最终报告记录精确测量值、设备信息、固件 SHA 和已知限制。

## 11. 对现有文档的影响

本设计取代兼容性报告中“Task 8 必须等待 Task 7 硬件通过后才能开始”的执行顺序，但不取代 Task 7 的验收门禁。Task 8、9、11 和 Task 10 的准备工作可以提前完成；正式切换、旧路径删除和发布验收仍必须等待硬件兼容性及完整回归通过。

后续详细实施计划应更新 Deferred Hardware Validation Mode、Task 7 至 Task 12 的依赖关系和复选框语义，使“代码完成”“自动验证通过”“硬件验证通过”成为三个独立状态。
