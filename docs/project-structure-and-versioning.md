# OpenMV Vision 项目结构与版本控制设计

> V2 目标设计：共享 Core + Feature + Product，支持基础视频版和完整视觉版独立发布。

## 1. 项目目标

系统由三个协作部分组成：

1. OpenMV：视频采集、视频分析、视频传输。
2. ESP32：Wi-Fi、Web/API、OpenMV 通信、视频控制，以及可选的视频分析终端配置。
3. Python Client：设备管理、Wi-Fi 配置、视频查看，以及可选的视频分析终端配置。

两个产品方向：

- **Video Terminal / 基础视频终端**：基本 Wi-Fi、设备状态、视频查看和控制。
- **Vision Terminal / 完整视觉终端**：基础能力 + 视频分析和分析配置。

## 2. 目标结构

```text
openmv-vision-V2/
├── openmv/
│   ├── core/
│   ├── libraries/
│   │   └── video/
│   ├── features/
│   │   └── vision-analysis/
│   └── products/
│       ├── video-terminal/
│       └── vision-terminal/
├── esp32/
│   ├── core/
│   │   ├── wifi/
│   │   ├── api/
│   │   ├── web/
│   │   ├── openmv-link/
│   │   └── video/
│   ├── features/
│   │   └── vision-config/
│   └── products/
│       ├── video-terminal/
│       └── vision-terminal/
├── python-client/
│   ├── core/
│   │   ├── api/
│   │   ├── models/
│   │   ├── protocol/
│   │   └── video/
│   ├── features/
│   │   └── vision-config/
│   └── products/
│       ├── video-viewer/
│       └── vision-client/
├── protocols/
└── docs/
```

目录迁移不能为了整齐而改变已验证行为；实际拆分以源码依赖关系为准。

## 3. 依赖方向

推荐：

```text
Core → Feature → Product
```

Core 不反向依赖具体产品。产品负责组合 Core 和可选 Feature。

禁止通过长期 `lite` / `pro` 分支或复制 Core 代码维护两套系统。

## 4. ESP32 产品模型

```text
Video Terminal
    = ESP32 Core

Vision Terminal
    = ESP32 Core + Vision Config Feature
```

Core 包括 Wi-Fi、OpenMV 通信、视频控制、基础状态、基础 HTTP/API、基础 Web 和视频公共能力。Vision Feature 包括分析配置、分析 API、分析 Web 和相关状态/缓存。

## 5. Python 产品模型

```text
Video Viewer
    = Python Core + Video UI

Vision Client
    = Python Core + Video UI + Vision Feature
```

Python Core 包括登录框架、Device API、状态查询、Wi-Fi、视频接收/显示和启停。Vision Feature 包括分析配置 API、模型、页面和控件。

## 6. OpenMV 产品模型

OpenMV 视频传输实现应独立为视频库/核心组件，与 Vision/Application 解耦：

```text
OpenMV Video Library
        ↑
Vision / Application
```

第一版只冻结当前已验证的 QVGA/JPEG/SPI V2 行为；未来 VGA 等能力通过库版本扩展，不复制 QVGA 实现。

## 7. API/Web 分层

基础产品共享 Core API/Web；完整产品额外注册 Vision API/Web Feature。实际路径必须以现有源码为准，重构不得擅自修改接口语义。

## 8. Git 策略

- Branch：开发和评审。
- Directory/Feature：产品能力差异。
- Tag：组件/产品版本。
- Release：独立发行物。

不建立长期 `lite` / `pro` 产品分支。

## 9. 独立版本

推荐：

```text
openmv-video-v0.1.0
esp32-video-terminal-v1.0.0
esp32-vision-terminal-v1.0.0
python-video-viewer-v1.0.0
python-vision-client-v1.0.0
```

各组件版本独立；最终通过兼容矩阵记录精确组合。

## 10. 重构原则

1. 先分析，后重构。
2. 已验证功能优先保持行为不变。
3. 不因为目录调整改变协议、API、Web 或视频行为。
4. 不复制 Core 到两套产品。
5. Vision Feature 不污染基础视频终端。
6. OpenMV 视频库不依赖 Vision 业务。
7. Python UI 重构与设备功能重构分开。
8. 代码提交/合并不等于硬件验证通过。
9. 每次迁移都要有回归检查和验证记录。
