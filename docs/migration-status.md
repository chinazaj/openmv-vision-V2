# V2 迁移状态

> 本表是迁移工作的唯一进度跟踪入口。状态只有在对应验收条件满足后才能推进。

## 状态定义

| 状态 | 含义 |
|---|---|
| `NOT_STARTED` | 尚未开始 |
| `ANALYZING` | 已完成源码/依赖分析，准备迁移 |
| `MIGRATING` | 正在迁移 |
| `STATIC_CHECK` | 已迁移，正在进行语法、编译、静态检查 |
| `HW_VERIFY` | 已通过静态检查，等待或正在硬件验证 |
| `VERIFIED` | 功能验证完成，可作为后续模块依赖 |
| `BLOCKED` | 被明确问题阻塞 |

## 总体进度

| 阶段 | 组件 | 子系统 | 状态 | 验收标准 | 备注 |
|---|---|---|---|---|---|
| 0 | 全局 | V2 目录骨架 | `VERIFIED` | 基础目录、README、文档入口完成 | V2 已初始化 |
| 0 | 全局 | 迁移状态管理 | `VERIFIED` | 本表建立并持续维护 | 本文件 |
| 0 | 全局 | 协议基线 | `ANALYZING` | UART/SPI/HTTP/MJPEG 约束确认 | 迁移前冻结 |
| 1 | OpenMV | 源码与依赖盘点 | `VERIFIED` | 入口、类、配置、协议、视频依赖已登记 | Video Core 抽取无结构性阻塞 |
| 1 | OpenMV | Video Core | `MIGRATING` | V1 公共 API、SPI V2 协议行为与测试用例完成；随后执行静态测试和硬件验证 | 已收敛公共 API；新增 `docs/openmv-video-core-api.md` 与 `tests/test_video_core.py`；本环境未能联网克隆仓库，测试尚未实际运行，硬件验证待执行 |
| 1 | OpenMV | UART Core | `NOT_STARTED` | JSONL 接收、命令分发、响应行为一致 | 115200 8N1 |
| 1 | OpenMV | Config Core | `NOT_STARTED` | config.json 权威持久化与校验行为一致 | 不改变配置语义 |
| 1 | OpenMV | Vision Feature | `NOT_STARTED` | QR/颜色/形状检测行为与旧版一致 | 与视频核心解耦 |
| 1 | OpenMV | Servo Feature | `NOT_STARTED` | 舵机控制行为与旧版一致 | 非阻塞控制 |
| 1 | OpenMV | Video Product | `NOT_STARTED` | 基础视频固件可独立运行 | 不含 Vision 配置 |
| 1 | OpenMV | Vision Product | `NOT_STARTED` | 完整视觉固件可独立运行 | 包含 Vision |
| 2 | ESP32 | 源码与依赖盘点 | `NOT_STARTED` | Wi-Fi/API/SPI/UART/Web 依赖登记 | 依赖 OpenMV 基线 |
| 2 | ESP32 | Platform/Core | `NOT_STARTED` | 启动、Wi-Fi、配置基础能力稳定 | 基础版共用 |
| 2 | ESP32 | SPI Video Receiver | `NOT_STARTED` | V2 SPI 组帧、CRC、丢帧策略保持一致 | 最新帧优先 |
| 2 | ESP32 | MJPEG Server | `NOT_STARTED` | HTTP 81 视频流行为一致 | 基础版共用 |
| 2 | ESP32 | HTTP API Core | `NOT_STARTED` | 基础 API 行为一致 | 不含 Vision API |
| 2 | ESP32 | Web Core | `NOT_STARTED` | 基础 Web/Wi-Fi 配置可运行 | 基础版共用 |
| 2 | ESP32 | Vision Feature/API/Web | `NOT_STARTED` | 分析配置独立于 Core | 完整版 |
| 2 | ESP32 | Video Product | `NOT_STARTED` | 简单视频终端固件可独立构建 | 基础版 |
| 2 | ESP32 | Vision Product | `NOT_STARTED` | 完整视频分析终端可独立构建 | 完整版 |
| 3 | Python | 源码与依赖盘点 | `NOT_STARTED` | API/UI/视频线程依赖登记 | 最后迁移 |
| 3 | Python | Client Core | `NOT_STARTED` | 登录、设备连接、基础 API 能力稳定 | 基础版共用 |
| 3 | Python | Video Receiver | `NOT_STARTED` | MJPEG 解析、最新帧优先、重连行为一致 | 基础版共用 |
| 3 | Python | Basic UI/Product | `NOT_STARTED` | 登录、Wi-Fi、视频查看、启停完整 | Video Viewer |
| 3 | Python | Vision Feature | `NOT_STARTED` | 分析配置 API/UI 与 Core 解耦 | 完整版 |
| 3 | Python | Vision Product | `NOT_STARTED` | 完整视觉客户端可独立运行 | Vision Client |
| 4 | 系统 | Compatibility Matrix | `NOT_STARTED` | OpenMV/ESP32/Python 版本兼容关系明确 | 发布前必须完成 |
| 4 | 系统 | E2E 基础视频链路 | `NOT_STARTED` | OpenMV → SPI → ESP32 → MJPEG → Python | 基础版金标准 |
| 4 | 系统 | E2E Vision 链路 | `NOT_STARTED` | 配置下发、Vision、视频、状态闭环 | 完整版 |
| 4 | 系统 | Release/Tag | `NOT_STARTED` | 两类产品和组件可独立发布 | 最终阶段 |

## 当前阶段

**阶段 1：OpenMV Video Core 迁移。**

原仓库 OpenMV 源码盘点已完成，确认视频发送逻辑可以独立抽取，不需要修改 Vision、UART、Config 或 Servo 的内部行为。已按 V1 API 合同收敛 Video Core 公共接口，并增加 API 文档与 CPython 硬件桩测试。当前测试用例尚未在仓库工作区实际运行；先在本地执行文档中的 unittest 命令并修复问题，再进行 OpenMV/ESP32 硬件验证。验证完成前不将此模块标记为 `VERIFIED`。

## 迁移记录

| 日期 | 组件 | 变更 | 状态 |
|---|---|---|---|
| 2026-10-22 | V2 | 创建新仓库并初始化迁移文档 | `VERIFIED` |
| 2026-10-22 | OpenMV | 完成原始 OpenMV 视频链路分析，确认 `SPITransmitter` 可独立库化 | `VERIFIED` |
| 2026-10-22 | OpenMV Video Core | 收敛 V1 公共 API；补充 API 合同、README 与主机端硬件桩测试 | `MIGRATING` |
