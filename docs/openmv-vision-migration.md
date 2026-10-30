# OpenMV Vision 迁移映射与边界

## 1. 参考基线

- 原始仓库：`chinazaj/openmv-vision`
- 原始入口：`openmv-firmware/OpenMV_WIFI_V0_1.py`
- V2 目标分支：`feature/openmv-vision`
- 迁移依据：`docs/migration-plan.md`、`docs/migration-status.md`

原版是单文件固件。本次按职责拆分，不复制旧版 SPI 实现；SPI 视频发送继续使用 V2 已有的 `openmv/core/video/video_core.py`。

## 2. 源码映射

| V1 单体代码 | V2 目标 | 责任 |
|---|---|---|
| `SerialCommunicator` | `openmv/core/uart/uart_core.py` | UART3 JSONL 字节接收、LF 分帧、噪声重同步、超长行丢弃、JSON 解码和响应发送 |
| `ConfigManager` | `openmv/core/config/config_core.py` | config.json 默认值、schema 迁移、校验、整体/逐项读写、写后回读 |
| `VisionDetector`、`hex_to_lab_threshold` | `openmv/features/vision/vision_feature.py` | QR/颜色/形状检测及控制二维码扫描 |
| `DualServoController` | `openmv/features/servo/servo_feature.py` | 双舵机非阻塞动作状态机 |
| 原文件初始化与主循环 | `openmv/products/vision-terminal/main.py` | 组合 Core/Feature、相机生命周期、命令语义、检测事件和视频发送 |
| 原文件 `SPITransmitter` | 不迁移 | 由 V2 `VideoTransmitter` 替代，避免维护两套 SPI 实现 |

## 3. UART 行为合同

- UART3，115200 baud，8N1。
- UTF-8 JSON object，每行以 LF 终止；兼容 CRLF。
- 丢弃 JSON 起始符 `{` 之前的噪声，并在下一行重新同步。
- 单行最大长度 8192 bytes；超长行丢弃到 LF。
- 保留命令：`sync_config`、`apply_config`、`set_config_item`、`get_config_item`、`set_video`、`get_status`、`get_config`。
- 保留原有响应、事件字段及错误 reason。

UART Core 只负责传输和 JSONL framing；命令语义留在 Vision Product 的 handler，避免 Core 反向依赖配置或舵机业务。

## 4. Config 行为合同

`config.json` 仍是 OpenMV 设备侧权威持久化文件。保留：

- `schema_version=2` 及旧 schema_version=1 舵机角度顺序迁移。
- `video.enabled`。
- `analysis.mode`：`none/qr/color/shape`。
- `analysis.targets.color/shape/qr`：每类最多四个 slot。
- `control_qr` 的启用状态和视频开关 payload。
- `servo.s1/s2`、`bind.color/shape/qr`、`qr_type`、`sort_delay_ms`。
- `set_config_item` 的字段白名单、索引范围和规范化回读。
- JSON 写入后重新读取并比较。

本迁移不引入 Wi-Fi SSID/密码或 ESP32 网络配置，也不把 ESP32 NVS 缓存当作 OpenMV 配置权威源。

## 5. Vision 行为合同

- 业务分析模式互斥：QR、颜色、形状。
- 保留每三帧扫描一次 reserved control QR 的节奏。
- 控制二维码与普通业务二维码匹配隔离。
- 保留颜色 HEX→CIELAB 阈值算法、blob 参数和首个匹配目标策略。
- 保留圆形/矩形检测阈值和绘制颜色。
- 保留 detection index 输出，由 Product 依据 `bind` 配置映射舵机动作。

## 6. 当前限制与待验证

1. 本次拆分尚未完成 OpenMV MicroPython 目标版本的语法和导入检查。
2. 尚未在 OpenMV 板上验证目录部署、UART 分包/超长行处理及配置读写。
3. 尚未验证 ESP32 侧对所有命令/响应字段的兼容性。
4. Vision 算法尚未进行相机实测；算法阈值目前按 V1 原样迁移。
5. Video Core 的 SPI 参数和时序由现有 V2 Core 决定；本次不把 V1 单体文件中的旧 SPI 初始化复制回来。

上述项目通过前，迁移状态保持 `MIGRATING`，不能标记 `VERIFIED`。
