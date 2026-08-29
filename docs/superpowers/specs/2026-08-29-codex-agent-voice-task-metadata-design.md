# Codex Agent / Voice 与任务元数据设计

## 目标与视觉基准

- 唯一视觉基准是 `docs/prototypes/codex-status-telemetry-layout.html?variant=G3`，目标画布为 466 × 466。
- 上半部只替换既有 Model / Effort：`MODEL` 改为 `AGENT`，值改为当前已认证 Agent 名称；`EFFORT` 改为 `VOICE`，值改为 Voice 状态。
- 复用既有 `kTelemetryX`、`kTelemetryWidth`、四个 Y 坐标、Montserrat 14 标题字体、3 px 字距和状态颜色。Agent 名称从 26 px 开始逐级缩小到 24、22、20 px，20 px 仍放不下时才单行尾部省略；绝不换行或侵入 Voice 行。
- 下半部保持任务序号、Pending/Running、配额、分页和电池原样。项目名使用既有坐标并改为更大的粗体；任务名称使用约 19 px 的粗体中文显示。任务名称、模型文字和 Effort 徽标放入同一个透明 Flex 行，按视觉中心线对齐；该行最终位于 `y = 372`，与项目名留出明确间距。
- 模型显示只删除小写 `gpt-` 前缀，例如 `gpt-5.6-sol` 显示为 `5.6-sol`。模型使用无边框的 12 px 普通文字，Effort 使用 10 px 细描边徽标；模型文字使用自然高度，避免固定高容器造成垂直错位。空间不足时任务名先省略，模型与 Effort 保持可见。

## 数据流

1. Mac Agent 从 Codex `state_5.sqlite` 的 `threads` 表按每个活动任务的 `thread_id` 查询 `model` 与 `reasoning_effort`。
2. `codex_alerts` 的每一项增加 `model` 和 `effort`，并经过现有长度限制与 2048 字节消息预算。
3. 固件 `CodexTaskAlert` 保存每任务的 Model/Effort；解析器同时接受旧四字段和新六字段任务，保证 Agent 与固件滚动升级兼容。
4. 主页面轮播当前任务时，同步更新任务名、精简模型名与 Effort 徽标。
5. Agent 名称不增加新协议字段。固件使用活动生产连接的 fingerprint 查找已签名、已持久化的 Agent registry 名称。
6. Voice 值继续由现有 `VoiceInputUiState` 和连接状态归约得到。

## 状态规则

- Agent：活动连接显示 registry 名称；连接中显示 `CONNECTING`；断开显示 `UNAVAILABLE`；错误显示 `ERROR`。
- Voice：可用显示 `READY`；启动中显示 `CONNECTING`；采集中显示 `ACTIVE`；不可用与显式错误在界面上统一显示 `ERROR`，内部状态仍保留区别用于控制与诊断。
- Voice 文字前保留 7 px 状态圆点：`READY` 为薄荷绿、`CONNECTING` 为紫色、`ACTIVE` 为青色、`ERROR` 为红色。
- registry 名称过长时先逐级缩小至 20 px；仍超宽时保持单行并使用 dots 模式。

## 兼容与边界

- 保留顶层 `codex_model` / `codex_effort`，避免破坏旧消费者，但主页面不再直接显示它们。
- 任务缺少 Model/Effort 时仍显示任务本身，并隐藏空的模型文本或徽标。
- 旧四字段任务解析为 Model/Effort 为空；字段类型、UTF-8、长度、route 与状态继续 fail closed。
- 不改变 BLE UUID、认证、重放保护、任务详情加密、点击/语音生命周期或消息投递语义。

## 验收

- Agent 单元测试证明每任务映射正确、缺失字段安全、紧凑状态帧不超过 2048 字节。
- C++ 测试证明新旧任务格式均可解析，非法元数据被拒绝，轮播保留任务元数据。
- UI 合同测试证明遥测坐标不变、Agent 名称按“缩小优先、省略其次”处理、Voice 圆点与文字状态映射正确，并且任务名、12 px 无框模型文字与 10 px Effort 徽标按同一视觉中心线显示。
- 运行聚焦 Python 测试、Codex host 测试、`git diff --check` 和 ESP-IDF 构建。
- 浏览器原型只作为参考；固件 UI 在刷机后仍需真机 AMOLED 对照才能宣称视觉完成。
