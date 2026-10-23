# ESP32 Video Receiver Core

This module is the ESP32-C3 counterpart of the OpenMV `video_core` transport.

## Responsibility

`SpiVideoReceiver` owns the OpenMV SPI V2 receive path:

- SPI2 slave configuration
- GPIO mapping: SCK=4, MISO=5, MOSI=6, CS=7
- 18-byte SPI V2 header parsing
- 4096-byte payload handling
- 4-byte DMA transaction alignment
- frame reassembly
- frame-size and packet-order validation
- CRC16-CCITT validation
- two-frame latest-frame buffering
- receive statistics

It does **not** own Wi-Fi, HTTP, MJPEG presentation, camera capture, or OpenMV application commands.

## Public API

```cpp
SpiVideoReceiver video;
video.begin();

SpiVideoReceiver::FrameView frame;
if (video.acquireLatestFrame(frame)) {
    // Consume frame.data / frame.size.
    video.releaseFrame(frame);
}
```

`acquireLatestFrame()` always selects the newest completed frame. A frame is held until `releaseFrame()` is called; the receiver will never overwrite a buffer marked as being sent.

## Compatibility with OpenMV Video Core

The receiver intentionally matches the current OpenMV defaults:

- SPI bus: SPI2
- SPI mode: 0
- SPI payload: 4096 bytes
- maximum JPEG frame: 48 KiB
- SPI V2 header: 18 bytes, little-endian
- CRC16-CCITT: initial value `0xFFFF`, polynomial `0x1021`

## Integration product

`esp32/products/video-terminal/OpenMV_WIFI_C3_VideoTerminal.ino` is a deliberately small integration target for hardware testing. It provides:

- AP `openmv-wifi`
- `http://openmv-wifi.local/` when mDNS is available
- `/stream` MJPEG output
- `/api/status` receiver statistics

This product is a migration test target, not yet the final production firmware.
