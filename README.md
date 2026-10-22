# openmv-vision V2

这是 `openmv-vision` 的第二代迁移仓库，用于在不影响原项目稳定基线的前提下，逐步重构并迁移 OpenMV、ESP32-C3 和 Python Client。

## 迁移原则

- 原仓库 `openmv-vision` 作为稳定基线，不在迁移过程中随意修改。
- V2 采用全新目录结构，按模块逐步迁移。
- 每个迁移单元都必须经过代码检查、构建/语法检查以及必要的硬件验证后再标记完成。
- 保持现有稳定协议和行为，避免迁移与功能变更同时发生。
- 最终采用 Shared Core + Feature + Product 的结构支持基础版和完整视觉版独立发布。

## 推荐迁移顺序

1. **OpenMV**：先建立视频传输核心和视觉功能边界，冻结 UART/SPI 视频协议行为。
2. **ESP32-C3**：在 OpenMV 视频链路稳定基线之上拆分 Core / Video / Vision Feature / Product。
3. **Python Client**：最后迁移客户端，使其直接依赖已经稳定的 ESP32 API 与视频协议。
4. **系统联调与产品发布**：完成跨组件兼容矩阵、基础版/完整版本独立发布和回归测试。

这种顺序可以降低跨组件同时变化的风险；OpenMV 是视频源和协议上游，先稳定它可以减少 ESP32 与 Python 反复适配。

## 文档

- `docs/migration-status.md`：迁移状态总表和验收门槛
- `docs/migration-plan.md`：迁移阶段、顺序和策略
- `docs/architecture.md`：目标系统架构
- `docs/project-structure-and-versioning.md`：目录结构与版本策略
- `docs/dependency-and-refactor-analysis.md`：依赖关系与重构分析
