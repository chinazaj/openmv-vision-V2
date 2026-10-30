# OpenMV 部署规范：Flash / SD 卡与加密发布

> 文档状态：部署设计规范（待各目标板卡实机验证）  
> 适用仓库：`chinazaj/openmv-vision-V2`  
> 当前目标分支：`feature/openmv-vision`  
> 当前产品：OpenMV Vision Terminal

## 1. 目标与基本原则

本文定义 OpenMV 产品代码从 Git 仓库部署到设备文件系统的方式，覆盖两种存储布局：

1. **Flash 模式**：产品入口和公共模块都部署在 OpenMV 内部 Flash。
2. **SD 卡模式**：内部 Flash 只保留启动入口 `main.py`，公共模块和资源部署在 SD 卡。

两种方式均保持仓库内的模块化源码布局，不要求为了设备部署而改变 Python 包的组织方式。

部署必须遵循以下原则：

- 产品入口、公共 Core、Feature 和产品资源有明确边界。
- 部署脚本/流程必须完整同步依赖，不能只更新 `main.py`。
- 配置文件是设备运行数据，升级代码时默认不得覆盖用户配置。
- SD 卡挂载、模块版本检查和导入失败必须有明确错误处理。
- 不得把 `.mpy` 字节码等同于加密；发布加密必须采用明确的加密与运行时解密方案。
- 未在具体板卡、固件版本和存储介质上验证的方案，必须标记为待验证。

## 2. 仓库源码与设备路径映射

当前 Vision Terminal 源码位于 `openmv/`。推荐的部署映射如下：

| Git 仓库路径 | Flash 模式设备路径 | SD 卡模式设备路径 |
|---|---|---|
| `openmv/products/vision-terminal/main.py` | `/main.py` | `/main.py` |
| `openmv/core/` | `/lib/core/` | `/sd/lib/core/` |
| `openmv/features/` | `/lib/features/` | `/sd/lib/features/` |
| 产品配置文件（运行时生成/维护） | 按配置路径部署 | 推荐 `/sd/config/` 或产品约定路径 |
| 模型、字典等大型资源 | 视容量决定 | 推荐 `/sd/models/` 或 `/sd/assets/` |

不要把仓库中的整个 `openmv/` 目录原样复制到设备根目录。产品入口应复制为根目录的 `main.py`，公共模块目录映射到 `/lib` 或 SD 卡上的库目录。

### 2.1 Python 导入路径

Flash 模式在 `main.py` 中加入：

```python
import sys

if "/lib" not in sys.path:
    sys.path.insert(0, "/lib")
```

SD 卡模式在挂载 SD 卡成功后加入：

```python
import sys

SD_LIB = "/sd/lib"
if SD_LIB not in sys.path:
    sys.path.insert(0, SD_LIB)
```

随后两种模式都使用相同的包导入语句：

```python
from core.config.config_core import ConfigManager
from core.uart.uart_core import UARTJsonlTransport
from core.video.video_core import VideoTransmitter
from features.servo.servo_feature import DualServoController
from features.vision.vision_feature import VisionDetector
```

不要在业务模块中混用绝对设备路径导入或动态拼接模块路径。

## 3. 方案 A：全部部署到内部 Flash

### 3.1 设备目录

```text
/
├── main.py
├── config.json
└── lib/
    ├── core/
    │   ├── __init__.py
    │   ├── uart/
    │   ├── config/
    │   └── video/
    └── features/
        ├── __init__.py
        ├── servo/
        └── vision/
```

### 3.2 启动流程

1. Flash 启动 `/main.py`。
2. 将 `/lib` 加入 `sys.path`。
3. 导入 Core、Feature 模块。
4. 初始化配置、UART、相机、舵机和视频发送。
5. 进入产品主循环。

### 3.3 优点与限制

优点：

- 不依赖 SD 卡，启动链路简单。
- 模块读取不受 SD 卡状态影响。
- 适合模块体积较小、设备数量较少、现场不需要频繁换包的产品。

限制：

- 受内部 Flash 可用文件空间约束。
- 更新模块需要写入设备 Flash。
- 发布包、算法资源和模型增加后，容量压力会上升。
- 若设备进入异常状态，现场更新通常需要连接 IDE 或专用升级工具。

## 4. 方案 B：Flash 仅保留入口，模块存放 SD 卡

### 4.1 推荐设备目录

```text
OpenMV 内部 Flash
/
└── main.py

SD 卡挂载后
/sd/
├── lib/
│   ├── core/
│   │   ├── __init__.py
│   │   ├── uart/
│   │   ├── config/
│   │   └── video/
│   └── features/
│       ├── __init__.py
│       ├── servo/
│       └── vision/
├── config/
│   └── config.json
├── models/
├── assets/
└── version.json
```

建议把配置数据放在 SD 卡的独立 `config/` 目录，而不是放进只读/加密代码包。若现有 `ConfigManager` 当前使用固定的 `config.json` 路径，必须先确认并通过显式配置路径或产品级路径参数适配；不能仅移动文件而假设它会自动找到新位置。

### 4.2 启动流程

1. Flash 执行最小化的 `/main.py`。
2. 初始化板卡对应的 SD 卡接口并挂载到约定挂载点（本文统一使用 `/sd`）。
3. 检查 `/sd/lib`、版本清单和必要模块是否存在。
4. 将 `/sd/lib` 插入 `sys.path` 的前部。
5. 检查模块包版本与入口要求的兼容性。
6. 导入业务模块并启动 Vision Terminal。
7. SD 卡缺失、挂载失败、版本不兼容或关键模块缺失时，进入明确的启动故障状态，不得伪装成正常运行。

### 4.3 启动入口示意

以下仅展示启动顺序。SD 卡对象的创建方式、总线、引脚和挂载 API 必须由具体 OpenMV 型号及固件版本决定，不能将示例中的接口直接复制到所有板卡。

```python
# /main.py -- 启动骨架示意，非所有板卡通用的完整驱动代码
import sys
import os

SD_MOUNT = "/sd"
SD_LIB = SD_MOUNT + "/lib"

def mount_board_sdcard():
    # 在这里调用当前板卡/固件支持的 SD 卡驱动并执行 os.mount。
    # 返回成功挂载的文件系统对象或由驱动完成挂载。
    raise NotImplementedError("Implement for the target OpenMV board")

def prepare_runtime():
    mount_board_sdcard()

    if not os.stat(SD_LIB):
        raise RuntimeError("SD library directory is missing")

    if SD_LIB not in sys.path:
        sys.path.insert(0, SD_LIB)

prepare_runtime()

from core.config.config_core import ConfigManager
from core.uart.uart_core import UARTJsonlTransport
from core.video.video_core import VideoTransmitter
from features.servo.servo_feature import DualServoController
from features.vision.vision_feature import VisionDetector

# Continue with product initialization.
```

注意：上面的 `os.stat(SD_LIB)` 只是伪代码式目录检查示意。实际实现应使用目标 MicroPython/OpenMV 固件支持的目录检查方式，并区分“目录不存在”和“介质 I/O 错误”。

### 4.4 优点与限制

优点：

- 内部 Flash 占用小，启动入口可长期保持稳定。
- 模块、算法资源和模型容量可由 SD 卡扩展。
- 现场可通过更换受控发布包更新模块。
- 便于同一套公共库服务多个产品入口。

限制：

- 启动依赖 SD 卡及其文件系统。
- SD 卡拔出、损坏、接触不良或文件系统异常会影响运行。
- SD 卡读写速度和可靠性必须实测；不要在高频图像处理路径中频繁读取小文件。
- SD 卡中的模块更新必须具备完整性校验、版本检查和回滚策略。
- 不能假定所有 OpenMV 型号都具有相同 SD 卡接口、驱动或可用引脚。

## 5. OpenMV Cam H7 Plus 部署建议

### 5.1 推荐方案

对当前项目的 **OpenMV Cam H7 Plus**，建议优先评估并采用 **方案 B：Flash 保留 `main.py`，SD 卡保存公共模块和资源**。

理由：

- Vision Terminal 已拆分为多个 Core/Feature 模块，模块数量会继续增长。
- 后续发布包计划加密，代码包与用户配置/运行数据应分开管理。
- SD 卡便于现场更换版本化模块包，也能为模型和资源预留空间。
- Flash 中保留稳定入口，能减少业务模块迭代对启动文件的影响。

### 5.2 H7 Plus 的实施注意事项

- H7 Plus 的 SD 卡初始化必须以当前 OpenMV 固件提供的驱动/API 为准。
- 不要未经核对就使用其他 STM32 板卡的 `machine.SPI`、SPI 编号、CS 引脚或 `sdcard.py` 示例。
- 优先确认当前固件是否提供板级 SD 卡对象/SDIO 支持，以及官方固件所使用的挂载方式。
- 固定挂载点为 `/sd`，并在产品代码、部署工具和文档中保持一致。
- 验证 SD 卡格式、容量、文件系统、目录创建、读写和异常拔卡行为。
- 对视频持续发送场景，测试 SD 卡读写与相机采集/SPI 发送并行时的时延和稳定性。
- 若目标固件不能可靠支持 SD 卡挂载或现场环境不允许依赖可移动介质，则退回方案 A。

**状态说明：** 本文给出 H7 Plus 的推荐架构，但当前尚未声明 H7 Plus 的具体 SD 卡驱动代码已在本项目固件上验证。实际实施前必须记录 OpenMV 固件版本、SD 卡初始化代码、挂载点和实测结果。

## 6. 不同 OpenMV 硬件型号的部署选择

不能仅凭型号名称推断 SD 卡接口、驱动 API、引脚或文件系统行为。每个目标板卡都应建立独立的硬件部署配置。

| 硬件系列/型号 | 首选评估方案 | 关键核对项 | 当前状态 |
|---|---|---|---|
| OpenMV Cam H7 Plus | SD 卡模式（方案 B） | 板载 SD 卡接口、当前固件 SD 驱动/挂载 API、容量和并发 I/O | 架构建议，待实机验证 |
| OpenMV Cam H7 / H7 R2 等 STM32 H7 系列 | 先核对板卡是否提供可用 SD 接口；支持则评估方案 B，否则方案 A | 板卡修订、SD 接口、固件支持、引脚占用 | 待逐型号确认 |
| OpenMV RT1062 系列 | 依据具体板卡和固件的 SD/存储支持选择；不能照搬 H7 Plus 初始化代码 | 板级存储控制器、固件 API、文件系统和可用引脚 | 待逐型号确认 |
| 其他/未来型号 | 默认方案 A；通过硬件适配层验证后再启用方案 B | Flash 容量、SD 接口、挂载 API、性能和掉卡行为 | 待确认 |

推荐为每个硬件型号维护独立的部署参数，例如：

- `board_id`
- `firmware_version`
- `storage_backend`（`flash` / `sd`）
- `sd_mount_point`
- `sd_driver`
- `package_format`
- `minimum_app_version`

不要把板卡引脚和 SD 初始化细节散落在业务 Feature 中；应放在产品启动层或独立的 board/storage adapter 中。

## 7. 加密发布规范

用户明确要求最终发布的公共模块加密。此要求必须作为发布流水线的安全要求处理，不能把代码压缩、混淆或编译为 `.mpy` 当作加密。

### 7.1 必须区分的概念

| 方式 | 作用 | 是否等同加密 |
|---|---|---|
| Python 源码 `.py` | 可读源代码 | 否 |
| MicroPython 字节码 `.mpy` | 预编译/减少运行时解析成本，且与固件 ABI/版本相关 | 否 |
| 混淆/压缩 | 提高阅读门槛或减少体积 | 否 |
| 加密包 | 对静态存储内容进行保密，需密钥才能解密 | 是 |
| 运行时解密加载 | 在导入/执行前解密模块 | 取决于实现和密钥保护 |

### 7.2 推荐的发布包设计

建议发布流水线生成带清单的版本化包：

```text
release/
├── manifest.json
├── package.enc
└── signature.bin
```

清单至少记录：

- 产品 ID、硬件型号/硬件范围。
- 应用版本、公共库版本、协议版本。
- 最低兼容固件版本。
- 加密算法与格式版本标识（不包含密钥）。
- 文件路径、密文长度、完整性校验值。
- 签名算法和签名信息。

加密方案应采用经过审查的标准 AEAD 算法（例如 AES-GCM 或 ChaCha20-Poly1305），并对清单或整个发布包进行数字签名/认证。不要自行设计加密算法，也不要把密钥写进 Git 仓库、普通 `main.py`、SD 卡明文配置或发布包。

### 7.3 运行时解密的现实约束

MicroPython 的普通 `import` 不会自动加载自定义加密文件。若 SD 卡或 Flash 上只存密文，必须提供受信任的运行时加载器/固件支持，在模块导入前完成认证、解密和加载。

需要特别注意：

- 仅将文件加密后放在 SD 卡上并不能直接使用标准 `import`。
- 解密密钥如果以普通常量保存在 `main.py` 或 SD 卡中，攻击者可提取密钥，不能视为安全保护。
- 运行时解密后，代码或字节码可能在 RAM、解释器对象或调试接口中暴露；加密静态文件不等于防止运行时提取。
- `.mpy` 文件仍不是加密文件，而且必须匹配目标 MicroPython/OpenMV 固件支持的字节码版本和架构。
- SD 卡模式与 Flash 模式都需要相同的密钥管理和运行时加载策略；换存储介质本身不会增强加密强度。

如果威胁模型要求抵抗设备持有者读取固件/提取密钥，需要进一步评估芯片读保护、安全启动、密钥注入与硬件信任根能力。必须以具体 MCU、OpenMV 固件和量产流程为依据，不能仅依靠 Python 层隐藏密钥。

### 7.4 配置与用户数据

- 用户配置（如 `config.json`）与加密代码包分离。
- 不把 Wi-Fi 凭据、设备密钥或用户配置打包进公共模块发布包。
- 配置文件是否加密，应根据其中是否包含敏感数据单独决定。
- 升级程序不得无条件覆盖配置；应采用 schema 迁移和备份/恢复策略。
- 日志中不得输出密钥、明文凭据或解密后的敏感配置。

## 8. 更新、完整性与回滚

无论采用 Flash 还是 SD 卡，都应遵循以下更新流程：

1. 校验发布包签名和目标硬件型号。
2. 校验应用、库、协议和固件版本兼容性。
3. 将新包写入临时目录，不覆盖当前可用版本。
4. 校验写入后的长度、摘要和认证标签。
5. 原子切换当前版本指针/目录（若文件系统能力不支持原子切换，则采用双目录和启动标记方案）。
6. 启动自检成功后标记新版本有效。
7. 启动失败则回滚到上一已知可用版本。

SD 卡更新应保留上一版本目录，例如：

```text
/sd/packages/
├── active/
├── previous/
└── staging/
```

实际目录切换和原子性需按目标文件系统能力实现并验证。不得假定 FAT 文件系统支持 POSIX 原子目录交换。

## 9. 部署验收清单

### 9.1 通用验收

- [ ] 设备端目录与仓库映射一致。
- [ ] 所有包目录均包含需要的 `__init__.py`。
- [ ] 所有模块可在目标 OpenMV MicroPython 固件中导入。
- [ ] `sys.path` 顺序明确，且不会意外优先加载旧版本模块。
- [ ] 产品入口与公共库版本匹配。
- [ ] 配置文件升级时保留用户数据。
- [ ] UART、SPI 视频、Vision、Servo 行为完成回归。
- [ ] 设备重启后可恢复正确配置和模块版本。

### 9.2 SD 卡专项验收

- [ ] 冷启动时 SD 卡挂载成功。
- [ ] 无卡、坏卡、文件系统错误时能进入安全故障状态。
- [ ] 缺少模块或版本不兼容时拒绝启动。
- [ ] 运行中异常拔卡的行为明确且安全。
- [ ] 长时间视频运行时无明显采集/发送抖动或内存泄漏。
- [ ] 更新中断电后可恢复到旧版本或安全恢复模式。
- [ ] SD 卡文件损坏或认证失败时拒绝加载。

### 9.3 加密发布专项验收

- [ ] 发布物中不包含明文受保护模块。
- [ ] 使用标准 AEAD 加密并验证认证标签。
- [ ] 发布包/清单签名验证通过。
- [ ] 密钥未进入 Git、普通配置文件或明文发布包。
- [ ] 错误密钥、篡改密文、错误硬件型号均拒绝加载。
- [ ] 运行时加载器与目标固件版本匹配。
- [ ] 明确记录运行时提取风险和设备物理访问威胁模型。

## 10. 当前项目执行建议

当前开发阶段仍以可调试、可验证的模块源码为主。建议按以下顺序落地：

1. 先在 OpenMV Cam H7 Plus 上验证 SD 卡挂载 API、文件系统和 `/sd/lib` 导入。
2. 将 Vision Terminal 的入口改为“挂载存储 → 版本检查 → 设置 `sys.path` → 导入模块”的启动结构。
3. 为 Config Core 增加明确的配置文件路径参数，使配置位置可由产品部署配置决定。
4. 增加部署工具，将仓库 `openmv/core`、`openmv/features` 映射到目标存储目录。
5. 完成 Flash 模式和 SD 卡模式的导入、重启、视频运行与故障注入测试。
6. 再设计加密包格式、运行时解密加载器、密钥注入和签名验证；不要在加密方案未定前把 `.mpy` 当作最终加密产物。
7. 在硬件部署矩阵中记录每个型号的固件版本、驱动方式和验证结果。

在 H7 Plus 实机验证及安全加载方案完成前，SD 卡模式和加密发布均属于设计方案，不应标记为已验证或量产就绪。
