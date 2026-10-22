# OpenMV Video Core

`video_core.py` is a standalone OpenMV module for JPEG-over-SPI V2 transmission.

## Basic usage

Copy/import `video_core.py` as a normal OpenMV Python module:

```python
import sensor
from video_core import VideoTransmitter

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=2000)

video = VideoTransmitter(
    spi_bus=2,
    cs_pin="P3",
    baudrate=4000000,
)

while True:
    img = sensor.snapshot()
    video.send_frame(img)
```

The application owns the camera, main loop and decision to transmit. The module owns JPEG compression, frame ID, CRC16-CCITT, SPI V2 packetization and SPI transaction boundaries.

## Stable defaults

- QVGA is an application/camera setting; the module does not configure the camera.
- JPEG quality: `40`
- SPI: `4 MHz`, mode 0
- SPI V2 header: `18 bytes`, little-endian `<HBBHHHHIH`
- Payload: `4096 bytes`
- Maximum JPEG frame: `48 KiB`
- CRC: CRC16-CCITT (`0xFFFF` / `0x1021`)
- Header + payload + alignment padding: one CS LOW transaction

## Public API

- `VideoTransmitter(...)`
- `send_frame(img, quality=None)`
- `send_packet(...)`
- `get_stats()`
- `reset_stats()`
- `idle()`

No Vision, UART, ConfigManager, Servo or application state is required.
