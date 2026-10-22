# OpenMV Video Core 分析与抽取设计

## 1. 分析范围

分析基线为原仓库 `chinazaj/openmv-vision` 的 `openmv-firmware/OpenMV_WIFI_V0_1.py`（main，SHA `7e42ecbba3ed059cdf8408706164895047fed0e1`）。本次只分析视频采集后的 JPEG 压缩、SPI V2 分包和发送链路，不修改 Vision、UART、Config、Servo 业务。

## 2. 当前视频链路

当前入口把视频发送逻辑集中在 `SPITransmitter`：

1. 主循环调用 `sensor.snapshot()` 获取 `image.Image`。
2. VisionDetector 可以对同一帧做 QR/颜色/形状分析。
3. 当 `VIDEO_ENABLED` 为真时，调用 `spi_tx.send_frame(img, JPEG_QUALITY)`。
4. `SPITransmitter.send_frame()`：
   - `img.compress(quality=40)`；
   - 转换为不可变 `bytes` 快照；
   - 检查 `MAX_FRAME_SIZE=48 KiB`；
   - 分配并递增 16-bit `frame_id`；
   - CRC16-CCITT；
   - 按 `SPI_PAYLOAD_SIZE=4096` 分包；
   - 每包调用 `send_packet()`。
5. `send_packet()` 生成 18-byte SPI V2 header，并将 header + payload + 4-byte 对齐 padding 放在同一个 CS LOW transaction 中发送。

因此，视频发送功能已经具有天然的类边界，抽取不会要求修改其它业务类。

## 3. 视频 Core 与其它功能的真实依赖

### 3.1 必要依赖

`SPITransmitter` 实际只依赖 OpenMV 固件运行时提供的：

- `pyb.SPI`
- `pyb.Pin`
- `ustruct`
- `time`
- 输入帧对象的 `compress()` 方法

这些均可由一个独立模块封装。

### 3.2 不应继续依赖

抽取后的 Video Core 不应依赖：

- `ConfigManager`
- `VisionDetector`
- `SerialCommunicator`
- `DualServoController`
- `VIDEO_ENABLED`
- `config.json`
- UART JSONL 协议
- QR/颜色/形状业务
- 舵机
- 摄像头初始化 (`sensor.reset/set_framesize/...`)
- 主循环

尤其是 `VIDEO_ENABLED` 必须留在 Product/Application 层。Video Core 只负责“给我一帧图像，我就按既定协议发送”，是否发送由调用方决定。

## 4. 与 OpenMV 官方模块模式的对照

OpenMV 官方 `scripts/examples/12-Protocol/network_transport.py` 使用独立 `NetworkTransport` 类：构造函数完成底层资源初始化，公开 `read/write/size/flush/is_active` 等操作，业务协议通过模块实例调用，而不是把网络初始化和协议业务写死在主程序中。

本项目的 Video Core 采用相同思想：

- 一个可直接 import 的 Python 模块；
- 一个明确的传输类；
- 构造函数注入硬件参数；
- 公开稳定的发送方法；
- 不管理上层业务状态；
- 不要求调用者了解 SPI header、CRC、分包和 padding 细节。

## 5. 目标公共 API

建议模块提供 `VideoTransmitter` 类，第三方调用形态保持简单：

```python
from video_core import VideoTransmitter

video = VideoTransmitter(
    spi_bus=2,
    cs_pin="P3",
    baudrate=4000000,
)

img = sensor.snapshot()
video.send_frame(img)
```

主要方法：

- `send_frame(img, quality=None)`：压缩并发送一整帧；成功返回 `True`，当前帧因压缩、尺寸或 SPI 发送失败返回 `False`。
- `send_packet(...)`：保留为低层公共能力，便于未来需要直接发送已编码 JPEG 数据的产品使用，但普通调用者不需要使用它。
- `get_stats()`：读取发送/丢帧及最近一帧统计，不改变发送行为。
- `reset_stats()`：清零统计。

主要构造参数：

- `spi_bus=2`
- `cs_pin="P3"`
- `baudrate=4000000`
- `payload_size=4096`
- `max_frame_size=48*1024`
- `jpeg_quality=40`
- `spi_header` 协议常量固定在模块内，不作为普通业务参数暴露。

## 6. 必须冻结的协议/性能基线

第一次抽取只做结构变化，不改变已联调链路的行为：

| 项目 | 基线 |
|---|---|
| Camera format | RGB565 |
| Frame size | QVGA 320x240 |
| JPEG quality | 40 |
| SPI baudrate | 4 MHz |
| SPI protocol | V2 |
| Header size | 18 bytes |
| Header format | `<HBBHHHHIH` |
| Magic | `0x55AA` |
| Version | `2` |
| Type | `1` |
| Frame ID | uint16 |
| Payload | 4096 bytes |
| Max JPEG frame | 48 KiB |
| CRC | CRC16-CCITT, initial `0xFFFF`, polynomial `0x1021` |
| Transaction | one CS LOW for header + payload + alignment padding |
| Padding | 4-byte transaction alignment |
| Frame priority | latest-frame-oriented downstream behavior; transmitter does not introduce queueing |

## 7. 关键设计决定

### 7.1 不把 Camera 放进 Video Core

`VideoTransmitter` 接收 `img` 参数，而不是内部调用 `sensor.snapshot()`。这样同一个 Core 可以被：

- 视频产品调用；
- Vision 产品调用；
- 第三方自己的相机循环调用；
- 后续测试代码调用。

### 7.2 不把 VIDEO_ENABLED 放进 Video Core

基础产品可以直接：

```python
if video_enabled:
    video.send_frame(img)
```

视觉产品可以由 QR、配置或其它业务决定 `video_enabled`。Core 不关心来源。

### 7.3 不把 JPEG 质量写死为不可配置

默认值仍为 40，以保持旧版行为；允许实例级默认值和 `send_frame(..., quality=...)` 覆盖，方便第三方使用。但 V2 产品迁移默认继续使用 40。

### 7.4 不增加发送队列

当前实现是同步压缩、同步分包发送。第一次库化不引入线程、队列或异步状态机，避免改变延迟、内存和丢帧语义。

### 7.5 不改变 SPI 事务边界

这是最重要的硬件兼容约束。header + payload + padding 必须保持单次 `SPI.send()` 和同一 CS LOW 窗口。

## 8. 当前实现中发现的问题

没有发现阻止独立 Video Core 抽取的结构性问题。

发现以下需要在抽取时明确处理，但都属于边界清理，不需要改变协议：

1. 原类依赖模块级常量；抽取后这些常量归入模块或实例默认参数。
2. 原类通过全局 `print()` 包装受 `BUILD_PROFILE` 控制；Core 不应依赖全局 Build Profile，应改成可选 `debug`/日志回调或最简单地只保留异常统计而不强制输出。第一版建议保持最小依赖，不主动打印普通统计。
3. 原 `send_frame()` 同时负责 JPEG 压缩和传输。第一版继续保持该便利 API，同时把“发送已经编码好的 JPEG bytes”的能力作为低层方法，避免第三方必须重复实现 CRC/分包。
4. `dropped_frames` 在旧代码中既表示压缩/尺寸失败，也表示 SPI 失败。迁移时保持统计语义，不在第一版重新定义指标。
5. `MAX_FRAME_SIZE` 是接收端双缓冲能力的一部分。Video Core 默认值必须保持 48 KiB，不能因为库化而放宽。

## 9. 迁移后的调用边界

### Video Product

负责：

- `sensor` 初始化；
- 主循环；
- 是否发送；
- 视频状态；
- 与 UART/ESP32 的业务交互。

调用：

```python
video = VideoTransmitter(...)
...
img = sensor.snapshot()
if video_enabled:
    video.send_frame(img)
```

### Vision Product

负责：

- Camera；
- VisionDetector；
- ConfigManager；
- Servo；
- UART；
- Video enable/control；

同样只依赖 Video Core 的公开 API。

## 10. 结论

分析结论：**可以安全抽取，且应当现在抽取。**

`SPITransmitter` 是当前 OpenMV 代码中最适合首先库化的天然边界。抽取成独立 `video_core` 模块后，不需要修改 Config/Vision/Servo/UART 的内部实现，也不改变 SPI V2 协议。

迁移策略：

1. 先在 V2 创建独立 Video Core 模块；
2. 复制并最小化改造 `SPITransmitter`；
3. 用独立 API 保持旧版默认参数和协议完全一致；
4. 完成静态检查后，再由 Product/Application 接入；
5. 硬件验证通过之前，Video Core 不标记 `VERIFIED`。
