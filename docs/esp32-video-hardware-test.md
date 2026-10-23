# OpenMV → ESP32-C3 视频链路硬件联调

## 目标

验证当前两个 Core 的最小闭环：

```text
OpenMV video_core.py
    ↓ SPI V2
ESP32-C3 SpiVideoReceiver
    ↓ JPEG frame
ESP32-C3 /stream
    ↓ MJPEG
Browser
```

## 固件

OpenMV 使用当前已验证可运行的 `openmv/core/video/video_core.py`。

ESP32 使用：

`esp32/products/video-terminal/OpenMV_WIFI_C3_VideoTerminal.ino`

该测试固件只验证视频链路，不包含最终 Vision 配置、UART 控制和完整 Web 配置功能。

## SPI 接线

| OpenMV | ESP32-C3 | 说明 |
|---|---|---|
| SPI SCK | GPIO4 | SPI clock |
| SPI MISO | GPIO5 | OpenMV → ESP32 |
| SPI MOSI | GPIO6 | ESP32 → OpenMV，当前协议实际主要用于事务时钟 |
| SPI CS | GPIO7 | ESP32 slave select |
| GND | GND | 必须共地 |

OpenMV Video Core 当前内部默认值：SPI2、CS=P3、4 MHz、4096-byte payload、48 KiB 最大帧、JPEG quality=40。

## 测试步骤

1. 将 `video_core.py` 部署为 OpenMV `/lib/video_core.py`。
2. 启动 OpenMV 视频产品，使其持续执行 `sensor.snapshot()` + `video.send_frame(img)`。
3. 编译并烧录 ESP32 Video Terminal 测试固件。
4. 打开串口监视器，确认出现：
   - `[VIDEO RX] SPI2 slave ready: SCK=4 MISO=5 MOSI=6 CS=7`
   - `[HTTP] server ready`
5. 手机/电脑连接 ESP32 的 `openmv-wifi` AP。
6. 打开 `http://192.168.4.1/`；如果 mDNS 可用，也可打开 `http://openmv-wifi.local/`。
7. 页面中的 MJPEG 图像应持续刷新。
8. `/api/status` 中 `rx_ok_frames` 应持续增加。

## 验收指标

### 必须通过

- 页面能够稳定显示 OpenMV JPEG 图像。
- `rx_ok_frames` 持续增长。
- 正常运行时 `rx_bad_header_count`、`rx_bad_frames` 不应持续快速增长。
- `rx_oversize_count` 应保持 0。
- 视频停止后 5 秒内 HTTP 流应退出，重新开始发送后浏览器可恢复画面。

### 重点观察

- 长时间运行是否出现画面冻结。
- `rx_dropped_frames` 是否异常增长。
- `rx_frame_id_gap_count` 是否异常增长。
- OpenMV 是否出现 `send_frame()` 返回 `False` 的持续失败。
- ESP32 是否发生重启、堆内存不足或 SPI 错误。

## 当前限制

这是迁移阶段的最小联调产品，不代表最终 ESP32 产品架构已经完成。下一步仍需要把 Wi-Fi 管理、HTTP API、Web、OpenMV UART 和 Vision Feature 从旧单体固件中逐步拆入 `esp32/core` / `esp32/features` / `esp32/products`。
