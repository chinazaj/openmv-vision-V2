# OpenMV Video Core

`video_core.py` is a standalone OpenMV module for JPEG-over-SPI V2 transmission.

## Basic usage

Copy `openmv/core/video/video_core.py` to the OpenMV device as `/lib/video_core.py`, then import it using the stable public module name:

```python
import sensor
from video_core import VideoTransmitter

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=2000)

video = VideoTransmitter()

while True:
    img = sensor.snapshot()
    video.send_frame(img)
```

The application owns camera initialization, frame capture, the main loop, and the decision to transmit. The module owns JPEG compression, frame IDs, CRC16-CCITT, SPI V2 packetization, alignment padding, and SPI transaction boundaries.

## V1 public API

- `VideoTransmitter()`
- `send_frame(img) -> bool`
- `get_stats() -> dict`
- `reset_stats() -> None`

The constructor intentionally accepts no parameters. V1 does not expose transport or hardware tuning options. See [Video Core API contract](../../../docs/openmv-video-core-api.md) for the rationale, defaults, semantics, and test instructions.

## Internal defaults and protocol

- Fixed hardware: SPI2, CS GPIO P3
- SPI: 4 MHz, mode 0
- JPEG quality: 40
- SPI V2 header: 18 bytes, little-endian `<HBBHHHHIH`
- Payload per packet: 4096 bytes
- Maximum compressed frame: 48 KiB
- CRC: CRC16-CCITT (`0xFFFF` initial value, `0x1021` polynomial)
- Header + payload + alignment padding: one CS-low transaction

These are implementation defaults, not supported public runtime configuration.

## Boundaries

No camera initialization, `sensor.snapshot()`, vision analysis, UART, ConfigManager, servo control, application state, pause/resume, or persistent configuration is included. The product/application decides whether and when to call `send_frame(img)`.

Host-side contract tests live in `tests/test_video_core.py`. They use hardware stubs and do not replace OpenMV/ESP32 hardware verification.
