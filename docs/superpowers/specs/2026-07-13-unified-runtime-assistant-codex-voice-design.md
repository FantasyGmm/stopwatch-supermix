# StopWatch 统一运行时与 Assistant 合并设计

**日期：** 2026-07-13

**状态：** 已确认

**目标平台：** M5Stack StopWatch，ESP32-S3，16 MB Flash

**当前系统：** ESP-IDF 5.5.4、FreeRTOS、Mooncake、LVGL、StopWatch HAL、Xiaozhi Assistant

## 1. 目标

将当前分别运行在 `ota_0` 和 `ota_1` 的 Launcher 与 Xiaozhi Assistant 合并为一个固件，并满足以下目标：

1. 用户从 Launcher 打开 Assistant 时不重启设备。
2. 用户退出 Assistant 时直接返回 Launcher，不重启设备。
3. Assistant 退出后，其协议、网络会话、音频处理、定时器和后台任务完全停止。
4. 保留现有 Mooncake 应用体系、Launcher、LVGL UI 和 StopWatch HAL。
5. 选择性引入 ESP-Brookesia 的服务与 Agent 组件，统一管理 Wi-Fi、音频和 Agent 生命周期。
6. 为后续 Codex 通过 Wi-Fi 将手表录音发送到关联 Mac 的能力建立共享服务边界。
7. 第一阶段继续使用 USB/串口刷写；架构上为后续 Recovery 远程升级保留接口。

## 2. 已确认决策

### 2.1 系统组合

采用以下组合，不整体更换操作系统：

```text
ESP-IDF + FreeRTOS
        │
        ├─ Mooncake：应用和前台界面生命周期
        ├─ Brookesia：共享服务和 Agent 生命周期
        └─ StopWatch HAL：硬件驱动和板级资源
```

### 2.2 明确不采用

- 不迁移到 Zephyr、NuttX 或其他操作系统。
- 不替换 ESP-IDF 或 FreeRTOS。
- 不替换现有 Mooncake Launcher。
- 不整体迁移到 Brookesia System Core。
- 不引入 Brookesia GUI Runtime、动态 App 包、App Store、JavaScript、Lua 或 WASM Runtime。
- 不让 Mooncake、Brookesia 和 Xiaozhi 各自重复初始化同一套硬件。
- 不使用外部 `vTaskDelete()` 强制终止仍持有资源的任务。

### 2.3 应用行为

- Assistant 打开和退出均不重启。
- Assistant 退出后完全停止，不在后台保持唤醒、监听或服务器连接。
- Codex 未来仅把手表作为麦克风；正式语音识别在关联 Mac 上完成。
- BLE 继续负责手表与 Mac 的关联和控制信息交换。
- Codex 正式音频通过局域网 Wi-Fi WebSocket 发送给 BLE 关联的 Mac。
- Codex 语音传输的鉴权与加密细节在后续 Codex 语音专项设计中确定；本阶段只定义稳定的 Transport 接口，不实现该传输。

### 2.4 升级策略

- 第一阶段采用单固件、单 App 分区和 USB/串口刷写。
- 第一阶段不保留 A/B OTA 自动回滚。
- 未来可以增加小型 Recovery 分区，由 Recovery 下载、校验并写入主固件。
- 当前业务代码不得继续依赖 `ota_0`、`ota_1` 等固定分区名称。

## 3. 当前基线

### 3.1 Flash

| 项目 | 当前值 |
|---|---:|
| Launcher 固件 | 4,283,824 字节 |
| Assistant 固件 | 2,907,312 字节 |
| 两个固件实际内容总计 | 7,191,136 字节 |
| `ota_0` 分区 | `0x4f0000`，约 4.94 MB |
| `ota_1` 分区 | `0x4f0000`，约 4.94 MB |
| 两个 App 分区预留总计 | 约 9.88 MB |
| `storage` | 1 MB |
| `assets` | 4 MB |

### 3.2 内部 RAM 链接基线

| 固件 | DIRAM 使用 |
|---|---:|
| Launcher | 160,711 字节 |
| Assistant | 153,627 字节 |

合并后不能直接叠加两套静态对象。Assistant 的大对象、队列、任务栈、AFE 模型和音频缓冲区必须按需创建，优先使用 PSRAM，并在退出后释放。

### 3.3 当前架构问题

- Launcher 和 Assistant 各有一个 `app_main()`。
- Launcher 使用 Mooncake 应用循环；Assistant 使用永不返回的 `Application::Run()`。
- Assistant 再次初始化 LCD、SPI、I2C、PMIC、按键、LVGL 和音频硬件。
- Assistant 依靠切换 boot partition 和 `esp_restart()` 进入或返回 Launcher。
- `AudioService::Stop()` 会设置停止标志并清空队列，但完整资源释放主要依赖对象析构。
- Assistant 的协议、激活任务、时钟定时器和网络 Board 缺少面向子应用的统一停止边界。
- Launcher 与 Assistant 使用不同的构建入口和组件集合，合并时存在 LVGL、Kconfig 和 managed components 冲突。

## 4. 总体架构

```text
Mooncake App Layer
├─ AppLauncher
├─ AppAssistant
├─ AppCodexStatus
├─ AppClock
├─ AppBadge
├─ AppImu
├─ AppFft
└─ AppSetup
        │
        ▼
RuntimeSupervisor
├─ 前台应用状态
├─ 服务申请与释放
├─ 独占资源仲裁
├─ 启停超时
└─ 错误恢复
        │
        ▼
Shared Service Layer
├─ WifiService                 引用计数
├─ AudioCaptureService         独占租约
├─ AudioPlaybackService        单一所有者或有序队列
├─ VoicePipelineService        AFE、VAD、重采样、Opus
├─ BleService                  保持现有 Mac 状态链路
├─ MacEndpointService          未来保存 Mac 端点与短期凭证
└─ AgentService                Xiaozhi Agent 生命周期
        │
        ▼
StopWatch HAL
├─ Display / LVGL
├─ ES8311 / I2S
├─ I2C / SPI
├─ Buttons
├─ PMIC
└─ BLE / Wi-Fi 硬件接口
```

## 5. 组件职责

### 5.1 Mooncake

Mooncake 继续负责：

- 应用安装和列表。
- 前台应用打开、运行和关闭。
- Launcher 与子应用之间的界面切换。
- 调用 `AppAssistant::onOpen()`、`onRunning()` 和 `onClose()`。

Mooncake 不直接管理 Wi-Fi、I2S、麦克风或 Xiaozhi 协议。

### 5.2 RuntimeSupervisor

`RuntimeSupervisor` 是应用与共享服务之间的策略层，负责：

- 记录当前前台重型应用。
- 为应用申请和释放服务 binding。
- 保证麦克风和 I2S Capture 同时只有一个所有者。
- 按顺序停止旧所有者，再授权给新所有者。
- 对 `start`、`stop` 和 `waitStopped` 设置超时。
- 暴露可测试的任务数、资源所有者和服务状态快照。
- 遇到不可安全恢复的硬件状态时显示错误，不通过外部强杀任务伪造成功。

Supervisor 不负责渲染 UI，也不承载音频数据平面。

### 5.3 Brookesia 组件

第一阶段只评估并选择性采用：

- `brookesia_service_manager`
- `brookesia_service_helper`
- `brookesia_agent_manager`
- Xiaozhi Agent 适配组件
- 适合当前 HAL 的 Wi-Fi、音频或设备服务接口
- Brookesia Task Scheduler、事件和状态机基础能力

组件版本必须锁定。若某个 Brookesia 具体服务与现有 StopWatch HAL 不兼容，则保留其生命周期接口和 Manager，使用 StopWatch 自有服务实现，不为了采用框架而重写稳定驱动。

### 5.4 StopWatch HAL

StopWatch HAL 是唯一板级硬件所有者，负责一次性初始化：

- 屏幕、LVGL 和显示总线。
- I2C、SPI、PMIC 和 IO Expander。
- ES8311、I2S 和功放控制。
- 物理按键。
- BLE 和 Wi-Fi 所需板级能力。

Assistant 不再构造自己的 `M5StackStopWatchBoard` 硬件实例。Xiaozhi 通过适配器访问 StopWatch HAL。

### 5.5 AssistantRuntime

将 Xiaozhi 当前的永久 `Application` 改造为可重入生命周期：

```cpp
class AssistantRuntime {
public:
    StartResult start(const AssistantDependencies& dependencies);
    void requestStop(StopReason reason);
    StopResult waitStopped(TickType_t timeout);
    AssistantRuntimeState state() const;
};
```

`start()` 只能从 `Stopped` 进入 `Starting`。`requestStop()` 必须幂等。`waitStopped()` 只有在协议、音频、定时器和子任务全部退出后才能返回成功。

### 5.6 VoicePipelineService

共享音频管线负责：

- 从 ES8311/I2S 获取 PCM。
- AFE、回声消除、降噪和 VAD。
- 输入重采样到 16 kHz。
- Opus 编码。
- 把编码后的帧交给当前 Transport。
- 接收远端 Opus 音频并播放。

音频管线不判断目标是 Xiaozhi 还是 Codex；目标差异由 Transport 处理。

### 5.7 Transport

```cpp
class VoiceTransport {
public:
    virtual ~VoiceTransport() = default;
    virtual StartResult start() = 0;
    virtual SendResult sendAudio(AudioFrame frame) = 0;
    virtual void requestStop() = 0;
    virtual StopResult waitStopped(TickType_t timeout) = 0;
};
```

- `XiaozhiVoiceTransport` 连接 Xiaozhi 服务端。
- 后续 `CodexMacVoiceTransport` 使用 BLE 下发的 Mac 端点，通过 Wi-Fi WebSocket 发送音频。
- 当前 Assistant 合并只实现和验证 `XiaozhiVoiceTransport`。

## 6. 资源所有权规则

| 资源 | 管理方式 | 规则 |
|---|---|---|
| Display / LVGL | StopWatch HAL | 系统常驻，应用只拥有自己的 LVGL 对象 |
| BLE | BleService | 保持现有 Mac 状态链路，应用不重复初始化 Controller |
| Wi-Fi | 引用计数 binding | 第一个 binding 启动，最后一个 binding 释放后停止 |
| 麦克风 / I2S Capture | 独占租约 | 同时只能有一个应用持有 |
| 扬声器 / I2S Playback | 有序所有权 | Assistant 对话期间归 Assistant；系统音效必须遵守切换策略 |
| AFE / Opus | VoicePipeline binding | 语音会话期间创建，结束后释放大缓冲区 |
| Xiaozhi 协议 | AssistantRuntime | 只在 Assistant 活跃时存在 |
| Codex Mac Transport | Codex 语音会话 | 后续实现，只在用户启动 Codex 语音时存在 |

Wi-Fi 底层是否停止由全部 binding 决定。Assistant 退出后即使 Wi-Fi 因其他应用仍运行，Assistant 自己的协议、回调、任务和服务 binding 也必须归零。

## 7. 生命周期和数据流

### 7.1 打开 Assistant

```text
AppAssistant::onOpen
→ RuntimeSupervisor::startAssistant
→ 获取 Wi-Fi binding
→ 获取 Audio Capture/Playback 独占租约
→ 创建 VoicePipeline
→ 创建 XiaozhiVoiceTransport
→ AssistantRuntime::start
→ UI 显示连接和激活状态
→ 进入可对话状态
```

UI 必须先进入 Assistant 页面，再异步连接网络，不能阻塞 Mooncake 主更新循环。

### 7.2 退出 Assistant

```text
用户触发返回
→ AppAssistant 请求停止
→ 禁止新的录音和播放请求
→ 停止唤醒、VAD 和音频输入
→ 关闭 Xiaozhi 对话和协议
→ 停止激活任务、定时器和事件回调
→ 清空并释放音频队列、AFE、Opus 和重采样器
→ 等待所有 Assistant 子任务自行退出
→ 释放 Audio 租约
→ 释放 Wi-Fi binding
→ 销毁 Assistant LVGL 对象
→ AppAssistant::close
→ 返回 Launcher
```

正常停止目标不超过 2 秒。若停止超时，界面显示错误并禁止把资源授权给另一个应用，直到资源状态恢复或用户执行受控重启。

### 7.3 Xiaozhi 语音路径

```text
麦克风
→ StopWatch Audio HAL
→ AFE / VAD / 16 kHz 重采样
→ Opus
→ XiaozhiVoiceTransport
→ Xiaozhi 服务端 STT / Agent / TTS
→ 返回文字和 Opus 音频
→ 手表显示与播放
```

本地只执行唤醒词、音频前处理和编解码，正式 STT 在 Xiaozhi 服务端完成。

### 7.4 后续 Codex 语音路径

```text
Mac 通过 BLE 与手表关联
→ BLE 下发局域网端点和短期凭证
→ 用户在 Codex 中启动语音
→ Codex 获取 Wi-Fi binding 和麦克风租约
→ 复用 VoicePipeline
→ CodexMacVoiceTransport 通过 WebSocket 发送 Opus
→ Mac 解码并完成 STT
→ Mac 将文字输入 Codex
→ 手表接收状态或识别文字
```

Codex 默认状态监控继续使用现有 BLE；只有语音会话使用 Wi-Fi 和麦克风。

## 8. 错误处理

### 8.1 Wi-Fi 失败

- Assistant UI 显示连接失败和重试状态。
- 不进入可录音状态。
- 用户退出时仍必须完整释放已创建的服务。

### 8.2 Xiaozhi 协议失败

- 停止当前录音发送。
- 关闭协议对象和回调。
- 允许在 Assistant 页面内受控重试。
- 退出 Assistant 不等待无限重连。

### 8.3 音频申请失败

- 如果麦克风已有所有者，不启动第二条音频管线。
- Supervisor 返回明确的 Busy 状态。
- UI 显示资源正被占用。

### 8.4 停止超时

- 不从外部强杀仍持有互斥锁、I2S 或堆对象的任务。
- 记录未退出的任务和仍持有的资源。
- 阻止其他应用获取冲突资源。
- 提供受控重启作为最终恢复路径。

### 8.5 BLE 与 Wi-Fi 共存

- 保留 BLE 连接时验证 Wi-Fi 吞吐和音频稳定性。
- 任务优先级和 CPU 亲和性必须通过实测确定。
- Wi-Fi 重连不得阻塞 Mooncake UI 更新或物理返回操作。

## 9. 构建和依赖策略

1. 保留主工程作为唯一 ESP-IDF project。
2. 将 Xiaozhi 所需源码按职责拆成 ESP-IDF components，不引入第二个 `app_main()`。
3. 统一使用一套 LVGL 版本和配置。
4. 只加入 M5Stack StopWatch 所需的 Xiaozhi board、codec、protocol 和 display adapter 源码。
5. 锁定 Brookesia 组件版本，并先建立最小可构建验证。
6. 不同时保留旧的 Assistant Board 初始化和新 HAL adapter。
7. 每个迁移阶段删除已被替代的旧路径，避免临时兼容层长期增加固件容量。

## 10. 分区和升级设计

第一阶段分区原则：

- 一个主 App 分区，建议先预留 7–7.5 MB。
- 保留 1 MB `storage`。
- 保留 4 MB `assets`。
- 保留 NVS、PHY、coredump 等必要数据分区。
- 删除通过 `ota_0` 和 `ota_1` 在 Launcher/Assistant 之间切换的逻辑。

改变分区偏移后需要一次 USB 全量刷写，并重新写入受影响的 assets。分区迁移工具不得假设旧 storage/assets 数据在新偏移自动可用。

未来 Recovery：

- 通过一次 USB 刷写加入小型 Recovery 分区。
- Recovery 负责联网、下载、签名或完整性校验以及写入主 App。
- 主应用只调用稳定的 `FirmwareUpdater` 接口请求进入 Recovery。
- Recovery 不属于当前合并实现范围。

## 11. 容量和内存目标

### 11.1 Flash 目标

| 阶段 | 目标 |
|---|---:|
| 初次可运行合并固件 | 5.9–6.5 MB |
| 清理重复资源后 | 5.6–6.2 MB |
| 后续图片和字体优化后 | 4.8–5.5 MB |

预期收益：

- 相比两个固件 7.19 MB 的实际内容，减少约 0.7–1.6 MB。
- 单 App 分区预留 7–7.5 MB 后，相比当前 9.88 MB 双 App 预留，释放约 2.3–2.9 MB Flash。

### 11.2 RAM 目标

- Assistant 未打开时，内部空闲堆相比当前 Launcher 基线减少不超过 20 KB。
- Assistant 退出后，内部堆与进入前差异不超过 4 KB。
- 连续进入退出 100 次，累计堆损失不超过 8 KB。
- Assistant 的大缓冲区和模型优先按需放入 PSRAM。
- Assistant 关闭后，其音频、协议和激活任务数量为零。

## 12. 实施阶段与工作量

按一名熟悉 ESP-IDF、C++ 和 FreeRTOS 的工程师估算：

| 阶段 | 工作 | 人日 |
|---|---|---:|
| 0 | Brookesia 组件版本和最小构建验证 | 2–3 |
| 1 | 统一主工程、组件、Kconfig 和 LVGL | 2–4 |
| 2 | 统一 StopWatch HAL 和 ES8311 适配 | 3–5 |
| 3 | RuntimeSupervisor 与共享服务层 | 3–5 |
| 4 | 音频可重复启停和完整释放 | 4–7 |
| 5 | AssistantRuntime 生命周期改造 | 5–8 |
| 6 | Assistant UI 和当前 LVGL 适配 | 2–4 |
| 7 | 单 App 分区、构建和刷写流程 | 1–2 |
| 8 | 稳定性、容量、内存和应用回归 | 4–7 |
| **净工作量** |  | **26–45** |

考虑框架兼容、硬件联调和竞态修复，项目排期按 30–50 人日更稳妥：

- 单人约 6–10 周。
- 两名工程师约 4–7 周。

后续 Codex Wi-Fi/语音单独估算 16–29 人日，不计入当前 Assistant 合并交付。

## 13. 里程碑

### M1：单工程构建

- 只有一个 `app_main()`。
- Xiaozhi 必要组件进入主工程。
- Launcher 正常启动。
- 产出第一版统一链接和容量报告。

### M2：Assistant 无重启运行

- 点击 Assistant 直接进入。
- 可以联网、唤醒、对话和播放。
- 返回操作不再切换分区或重启。
- 使用当前 StopWatch HAL 和 LVGL。

### M3：Assistant 完全停止

- 协议、音频、激活、定时器和回调全部停止。
- 第二次及后续进入可正常使用。
- 退出后不持有麦克风或 Xiaozhi 网络连接。
- 内存恢复达到验收目标。

### M4：单分区和容量回收

- 新单 App 分区表。
- USB 全量刷写包。
- 删除 Launcher/Assistant 的 `ota_0`、`ota_1` 切换代码。
- 确认 `storage` 和 `assets` 布局。

### M5：硬件稳定性验收

- 完成重复启停、断网、播放中退出、BLE/Wi-Fi 共存和其他应用回归。
- 输出最终 Flash、内部堆、PSRAM、任务数和启停耗时报告。

## 14. 验收标准

### 14.1 功能

- Assistant 从 Launcher 打开时不重启。
- Assistant 返回 Launcher 时不重启。
- 唤醒词、对话、文字显示和语音播放功能保持可用。
- Assistant 退出后可立即打开其他应用。
- Clock、Codex 状态、Badge、IMU、FFT 和 Setup 功能无回归。

### 14.2 生命周期

- 连续进入和退出 Assistant 100 次无崩溃。
- 录音中、播放中、连接中和断网重连中退出均能完成停止。
- 停止正常情况下不超过 2 秒。
- Assistant 退出后没有残留音频、协议或激活任务。

### 14.3 内存和容量

- 满足第 11 节的 Flash 和 RAM 目标。
- 无持续内部堆或 PSRAM 泄漏。
- 主固件低于最终主 App 分区容量并保留至少 15% 构建余量。

### 14.4 共存

- Mac BLE 状态连接存在时，Assistant Wi-Fi 和语音稳定。
- Wi-Fi 连接或重连不阻塞 Mooncake UI 和返回操作。
- Assistant 停止后 BLE 状态同步继续正常。

### 14.5 刷写

- 提供可重复的全量 USB 刷写命令。
- 分区表、主固件和 assets 的写入地址一致。
- 完整擦除和从当前双分区布局升级两种路径均经过验证。

## 15. 主要风险与缓解

### 15.1 Brookesia 版本快速演进

**风险：** 最新文档和旧 Release 之间存在差异。

**缓解：** 先做最小构建验证，锁定组件版本；只采用已验证的 Manager 和接口，不跟随 `master` 自动升级。

### 15.2 Xiaozhi 上游生命周期假设

**风险：** 上游默认设备启动后永久运行，停止路径覆盖不足。

**缓解：** 将生命周期改造集中在 adapter/runtime 层；为启停、超时和任务退出增加测试；记录必要的上游补丁。

### 15.3 音频任务和 I2S 竞态

**风险：** 退出时仍有任务持有 codec、队列或互斥锁。

**缓解：** 采用停止令牌、任务自行退出、显式 join/wait 和资源释放顺序；禁止外部强杀。

### 15.4 内部 RAM 不足

**风险：** 合并后静态 BSS 和常驻对象增加。

**缓解：** 大对象按需创建，缓冲区优先 PSRAM，持续输出 heap/task 快照，并设定严格退出后基线。

### 15.5 BLE/Wi-Fi 共存

**风险：** 并发使用射频、任务和内部 RAM 导致吞吐或延迟下降。

**缓解：** 保持现有 BLE 控制面低带宽；语音走 Wi-Fi；通过硬件测试调整任务优先级、buffer 和省电策略。

### 15.6 分区迁移

**风险：** storage/assets 偏移变化造成数据不可读。

**缓解：** 第一版采用明确的全量 USB 刷写流程，重新写入 assets；分区变更前备份需要保留的数据。

## 16. 后续工作边界

当前合并完成后，后续 Codex 语音项目在同一共享服务基础上实现：

- BLE Mac 端点和短期凭证协议。
- Watch 端 `CodexMacVoiceTransport`。
- Mac 端 WebSocket 音频接收、Opus 解码和 STT 接入。
- Codex 录音 UI、重连、错误状态和端到端延迟测试。
- 音频链路鉴权、加密和凭证轮换方案。

这些能力不阻塞当前 Assistant 合并，也不应在第一阶段以未验证的占位实现进入固件。
