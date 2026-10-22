# 系统架构设计

## 1. 系统组成

系统由 OpenMV 视觉节点、ESP32-C3 无线网关、Web 配置 UI 和 Python/PySide6 桌面客户端组成。

```text
Python/PySide6 客户端
  ├─ HTTP API Client ───────────────┐
  └─ MJPEG 接收线程                 │ HTTP :80 / MJPEG :81
                                   ▼
ESP32-C3 网关
  ├─ Wi-Fi AP/STA、mDNS、Web UI、HTTP API
  ├─ UART JSONL 命令/响应
  ├─ SPI Slave 接收 JPEG 分包
  ├─ 双帧缓冲、最新帧优先
  └─ Preferences/NVS 网关缓存
              │ UART 115200 / SPI V2
              ▼
OpenMV 视觉固件
  ├─ 相机采集、QR/颜色/形状识别
  ├─ 舵机非阻塞动作状态机
  ├─ ConfigManager
  └─ config.json 权威持久化
```

## 2. 模块职责

### OpenMV
入口：`openmv-firmware/OpenMV_WIFI_V0_1.py`。负责 RGB565/QVGA 图像采集、互斥业务分析模式（none/qr/color/shape）、控制二维码独立扫描、舵机控制、配置校验与持久化。UART3 使用 115200、8N1，JSONL 以 LF 结尾。JPEG 经 SPI Master 分包发送，帧大小上限 48 KiB。

### ESP32-C3
入口：`wifi-firmware/arduino/OpenMV_WIFI_C3_V0_1/OpenMV_WIFI_C3_V0_1.ino`。负责 AP/STA、mDNS（默认 `openmv-wifi.local`）、网页和 HTTP API。UART1 与 OpenMV 交换 JSONL；SPI Slave 组帧并检查帧头、长度、帧序号和 CRC。端口 80 提供控制/API，端口 81 提供 MJPEG。Preferences/NVS 保存 Wi-Fi 参数和 `vision_cfg_cache`。

### Web UI
HTML 由多个 `html_*.h` 文件组成。配置页逐项读取 OpenMV 当前值；保存后独立回读并比较。HTTP 接受写请求不等于 OpenMV 已保存成功。

### Python 客户端
`python-client/` 使用 PySide6 GUI、标准库 HTTP 客户端、QThread MJPEG 接收线程和 QLabel/QImage/QPixmap 显示，不依赖 Qt WebEngine。视频帧采用最新帧优先，断线后自动重连。

## 3. 关键数据流

### 配置读取/写入
调用方请求 ESP32 HTTP API；ESP32 经 UART 请求 OpenMV；OpenMV 返回运行时权威配置。写入时 OpenMV 校验候选值、写入 `config.json` 并重新读取验证；UI 再独立回读，只有值一致才提示成功。ESP32 NVS 缓存不是 OpenMV 文件的替代品。

### 视频
OpenMV 拍摄并压缩 JPEG → SPI V2 分包 → ESP32 校验并组装完整帧 → 双缓冲发布最新帧 → HTTP MJPEG 输出 → 桌面端解析 multipart 并显示 JPEG。

## 4. 状态与约束

- 视频状态以 OpenMV 经 UART 报告的状态为权威；ESP32 的 `video_active` 是镜像状态。
- HTTP 写配置返回 202 仅表示请求已接受/转发。
- 最新帧优先策略允许丢帧，以降低延迟，不承诺逐帧无损。
- UART 为 115200、8N1、LF 终止 JSON；SPI V2 使用 18 字节小端帧头、CRC16-CCITT、4096 字节 payload 上限和 48 KiB 帧上限。
- GPIO/接线必须以目标开发板和实际布线为准。
