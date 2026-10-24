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

## Development / Release mode

All video-core diagnostic code is isolated by `video_build_config.h`.

- `VIDEO_BUILD_MODE_DEVELOPMENT`: enables SPI transaction-length counters, header-error classification, sequence/frame validation counters, limited raw-header logging, and periodic diagnostic output.
- `VIDEO_BUILD_MODE_RELEASE`: removes those development-only counters and verbose diagnostics from the build.

The current branch is intentionally left in **DEVELOPMENT** mode while the SPI receive problem is being investigated. Before a production build, change `OPENMV_VIDEO_BUILD_MODE` to `VIDEO_BUILD_MODE_RELEASE` in `video_build_config.h`.

Diagnostics are deliberately rate-limited so that serial output does not become a new source of SPI timing disturbance.

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
