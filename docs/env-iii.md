# 环境监测

桌面新增独立的“环境监测”应用，适配
[M5Stack Unit ENV-III（U001-C）](https://docs.m5stack.com/en/unit/Unit_ENV-III)，
显示 SHT30 的温度、相对湿度和 QMP6988 的气压。界面使用中文。
数据在设备本地采集，不经过 Mac Agent 或 BLE。

## 接口与供电

官方 Grove Port A 引脚表如下：

| 线色 | 信号 | StopWatch Port A |
| --- | --- | --- |
| 黑 | GND | GND |
| 红 | 供电 | 5 V |
| 黄 | SDA | GPIO10 |
| 白 | SCL | GPIO11 |

引脚依据 [StopWatch 官方 PinMap](https://docs.m5stack.com/en/core/StopWatch)。
板载 I²C 使用 GPIO47 / GPIO48，外接接口不能复用它的句柄。Port A 使用另一个
I²C 控制器，仍由原有 HAL 统一管理；应用不创建总线或 PMIC 实例。
打开应用后，HAL 请求 Grove 5 V 供电；离开应用后恢复启用前的 BOOST 状态。

用户已确认传感器和官方配套附件在现有固件上正常工作。本轮只调整图标和数据显示，
不改动驱动、供电或接口，也不重新排查传感器。

模块地址分别为 SHT30 `0x44` 和 QMP6988 `0x70`。两者独立检测，不扫描整条总线。
ENV-IV、ENV-II 等使用不同芯片的型号不由此驱动适配。
模块官方环境工作范围为 0–60 °C；温度值反映传感器所在位置，也会受附近热源影响。

## 页面与自动恢复

- 在桌面打开“环境监测”。温度居中放大，下方并列湿度和气压。
- 页面不再重复显示应用标题、“实时”、“2 个传感器”或正常状态说明。
  温度、湿度和气压使用更大的数字；桌面图标改为简洁的温度计与水滴图形。
- 沿用 A+B 返回桌面。现有应用、按键与全局边缘手势保持原有语义。
- 独立后台任务约每秒采样一次，关闭页面后停止采样。I²C 单次事务超时为 50 ms，
  不在 LVGL 主循环里等待转换，不阻塞 Codex 任务更新和提醒。
- 未接入时显示 `--` 和简短的“离线”，约每五秒重新检测。
- 任一传感器读取失败时立即使它的数值失效，另一颗的有效数据继续显示。
  超过五秒的样本显示为过期，不会把上次读数当成实时数据。
- 重新接入后必须通过识别及有效采样才恢复数值。SHT30 的串号响应和每次读数均校验
  CRC；QMP6988 校验芯片 ID、校准参数、转换状态和结果范围。
- 接口或供电初始化失败显示“不可用”，不冒充正常读数。

本阶段不记录历史曲线，也不把传感器数据发送到 Mac，不新增网络或 BLE 服务。

## 来源、字库与构建

QMP6988 补偿算法适配 M5Stack M5Unit-ENV 的 MIT 代码，锁定提交
`737e5180ea7b84d8f22fe49ae4fc62cf06cbc3a2`。完整版权与许可见
[来源说明](../third_party_notices/m5unit-env.md)。没有引入整套 M5UnitUnified 或 Arduino 总线。

LVGL 的抗锯齿原本已开启，但旧数字字形只有 2 bpp（4 级覆盖），并在运行时放大
约 1.9 / 1.5 倍，会放大原有台阶。环境页现在改用原生 91 / 42 px 数字字形，取消
transform 放大；数字与中文均为 4 bpp（16 级灰度覆盖）。字体家族、页面布局和配色不变。

中文从已锁定 LVGL 的 Noto Sans SC 裁剪，数字为 Maple Mono Medium v7.9 子集。
固件构建直接使用受版本控制的 C 字库，无需额外下载字体。环境页字库重生成入口为
`main/apps/app_env_iii/assets/generate_fonts.sh`，通过 `STOPWATCH_MAPLE_FONT` 指定源 TTF；
桌面图标文字仍使用 `scripts/generate_env_fonts.sh`。工具版本、来源与 OFL 许可见
[字库来源](../third_party_notices/env-fonts.md)。

```sh
./scripts/build_fusion.sh
./scripts/export_fusion_firmware.sh --dest ../deliverables/firmware-env-iii
```

沿用 ESP-IDF 5.5.4、原分区和容量门禁。遵照用户要求，不新增或运行测试。
现有传感器、放大布局和新图标已由用户在真机确认可用；本轮只改文字栅格质量。
运行时堆和任务栈高水位尚未测量。

刷写需有针对候选固件的授权，无需先制作完整 16 MiB Flash 备份。执行时使用通过容量门禁的候选和实际检测到的完整 `/dev/cu.usbmodem*` 端口；只写 bootloader、分区表、应用与资源，不执行全片擦除，也不改写 NVS、配对凭据或 storage。写后核对各写入映像哈希，并从串口确认新固件启动。完整 Flash 备份脚本保留为可选恢复手段；实际写入和验证结果单独记录。
