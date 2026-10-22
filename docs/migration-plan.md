# V2 迁移计划

## 1. 为什么先 OpenMV

建议采用 **OpenMV → ESP32-C3 → Python Client**，用户提出的顺序是可行的，而且更适合本项目当前的视频链路。

原因：

1. OpenMV 是视频源，决定 JPEG 帧产生、SPI V2 分包和 UART 配置/控制的上游行为。
2. ESP32 依赖 OpenMV 的 SPI/UART 行为；先稳定 OpenMV 可以避免 ESP32 重构过程中同时猜测上游行为。
3. Python Client 依赖 ESP32 的 HTTP API 和 MJPEG，因此最后迁移可以减少 UI/API 反复调整。
4. OpenMV 代码量和硬件边界相对明确，适合作为第一块完成并冻结的“黄金基线”。

这并不意味着先重构 OpenMV 的所有业务。第一阶段的目标是**迁移并拆边界，保持现有行为**。

## 2. 迁移原则

### 2.1 先迁移，后优化

不得把“代码迁移”和“功能改造”混在一个大提交中。优先做到：

`旧代码行为 = V2 迁移代码行为`

确认一致后，再单独进行性能优化、接口优化或新功能开发。

### 2.2 先协议，后组件

在 OpenMV 阶段先确认并记录：

- UART JSONL：115200 / 8N1 / LF
- SPI V2：18-byte little-endian header
- CRC16-CCITT
- 4096-byte payload
- 单帧最大 48 KiB
- 帧 ID 与丢帧策略
- MJPEG HTTP 接口由 ESP32 阶段确认

### 2.3 每完成一个模块就形成可回退点

推荐每个迁移单元单独 commit，必要时通过 PR 合并。禁止积累大量未验证代码后一次性迁移。

## 3. 阶段顺序

### Phase 0 — 基础设施

- V2 仓库初始化
- docs 建立
- 迁移状态表建立
- 协议基线冻结
- 目标目录建立

### Phase 1 — OpenMV

顺序建议：

1. 源码/依赖盘点
2. Video Core
3. UART Core
4. Config Core
5. Servo Feature
6. Vision Feature
7. Video Product
8. Vision Product
9. OpenMV 硬件验证

其中 Video Core 优先，因为它是后续 ESP32 视频链路的上游。

### Phase 2 — ESP32-C3

顺序建议：

1. 源码/依赖盘点
2. Platform/Core
3. SPI Video Receiver
4. MJPEG Server
5. HTTP API Core
6. Web Core
7. Vision Feature/API/Web
8. Video Product
9. Vision Product
10. ESP32 硬件验证

### Phase 3 — Python Client

顺序建议：

1. 源码/依赖盘点
2. Client Core
3. Video Receiver
4. Basic UI/Product
5. Vision Feature
6. Vision Product
7. 客户端硬件联调

### Phase 4 — 系统验收

- Compatibility Matrix
- OpenMV → ESP32 → Python 基础视频链路
- Vision 配置闭环
- 断线/重连/丢帧/错误恢复
- 性能与内存检查
- 独立 Tag/Release

## 4. 关键验收门槛

一个模块只有同时满足以下条件才能标记 `VERIFIED`：

1. 代码结构符合目标架构。
2. 静态检查/语法检查通过。
3. 与旧版行为的差异已经明确。
4. 必要的硬件测试完成。
5. 不破坏已完成模块。
6. 迁移状态表已更新。

## 5. 基础版与完整视觉版策略

迁移期间不维护两个长期代码分支来代表基础版和完整版。

统一代码树使用：

- `core/`：两种产品共享
- `features/vision/`：完整视觉功能
- `products/video_terminal/`：基础视频产品
- `products/vision_terminal/`：完整视觉产品

最终通过产品入口、构建配置和 Tag/Release 实现独立发布。

## 6. 风险控制

### 高风险

- 修改 SPI/UART 协议
- 改变帧头字段含义
- 改变 JPEG 帧尺寸/压缩行为
- 同时重构 OpenMV 与 ESP32
- 同时修改 Python UI 和 ESP32 API

### 低风险

- 目录移动
- 类/模块拆分但保持接口
- 文档补充
- 添加测试/静态检查
- 产品入口包装

因此迁移初期明确禁止“顺手升级协议”。
