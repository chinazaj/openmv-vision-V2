# OpenMV Video Core API Contract

**Status:** V1 API contract for the V2 migration  
**Module:** `video_core.py`  
**Public import:** `from video_core import VideoTransmitter`

## 1. Purpose and responsibility boundary

Video Core implements one narrowly defined function:

`OpenMV Image -> JPEG compression -> SPI V2 frame transport`

It owns:

- JPEG compression using the fixed V1 quality;
- 16-bit frame sequence IDs;
- CRC16-CCITT over the complete compressed frame;
- SPI V2 header encoding and payload segmentation;
- four-byte transaction alignment padding;
- CS-low SPI transaction boundaries;
- transport counters and timing statistics.

It does not own:

- camera or sensor initialization and image capture;
- the decision to transmit, application FPS, or capture cadence;
- video enable/disable, pause/resume, or product state;
- vision analysis, QR/colour/shape detection, UART, servo control;
- application configuration or persistent configuration files;
- acknowledgement that the ESP32 has received or validated a frame.

This separation keeps the library reusable by both the Video Terminal and Vision Terminal products.

## 2. V1 public API

### Construction

```python
from video_core import VideoTransmitter

video = VideoTransmitter()
```

The constructor takes **no arguments** in V1. This is intentional: the current product hardware is fixed to SPI2 with CS on P3, and the wire protocol defaults are shared with the ESP32 receiver.

### Send one image

```python
ok = video.send_frame(img)
```

- `img` is an OpenMV image object supplied by the caller.
- JPEG quality is fixed internally at 40.
- `True` means compression succeeded and all packets were accepted by the OpenMV SPI driver.
- `False` means compression failed, the JPEG was empty/too large, or a packet send failed.
- Failed frames are dropped; no queue or retry is performed.
- `True` does **not** mean the ESP32 has received, reassembled, or CRC-validated the frame.

### Read statistics

```python
stats = video.get_stats()
```

Returned keys:

| Key | Meaning |
|---|---|
| `frame_id` | Next frame sequence value (16-bit wraparound) |
| `sent_frames` | Complete frames accepted by the OpenMV SPI driver |
| `dropped_frames` | Frames dropped due to compression, size validation, or SPI-send failure |
| `last_frame_size` | Size in bytes of the last successfully compressed frame, including a frame subsequently rejected for size |
| `last_send_ms` | Duration of the last successful frame send in milliseconds |
| `total_send_ms` | Sum of successful send durations since construction or last reset |

### Reset statistics

```python
video.reset_stats()
```

This resets runtime counters and timing statistics. It intentionally does **not** reset the frame sequence ID, reinitialize SPI, or change configuration.

## 3. V1 internal defaults

| Setting | V1 value | Policy |
|---|---:|---|
| SPI bus | SPI2 | Fixed hardware detail |
| CS pin | P3 | Fixed hardware detail |
| SPI baud rate | 4,000,000 Hz | Internal default |
| SPI mode | 0 | Protocol/hardware requirement |
| Payload per packet | 4096 bytes | Internal transport default |
| Maximum compressed frame | 48 KiB | Internal transport limit |
| JPEG quality | 40 | Internal image-quality default |
| Header format | `<HBBHHHHIH` | Wire protocol; 18 bytes |
| CRC | CRC16-CCITT, init `0xFFFF`, polynomial `0x1021` | Wire protocol |
| Alignment | Pad header + payload to a multiple of 4 bytes | SPI V2 transaction format |

The module does not configure camera resolution. For example, QVGA/RGB565 settings remain in the application.

The 48 KiB limit applies to the **compressed JPEG byte stream**, not the raw image dimensions. A frame exceeding this limit is dropped.

## 4. Public API stability rules

The only supported V1 public API is:

- `VideoTransmitter()`
- `send_frame(img)`
- `get_stats()`
- `reset_stats()`

The following are intentionally not public V1 APIs:

- constructor parameters such as `spi_bus`, `cs_pin`, `baudrate`, `payload_size`, `max_frame_size`, and `jpeg_quality`;
- `send_packet(...)`, CRC helpers, or other protocol implementation details;
- `configure(...)`, `set_xxx(...)`, or `get_config()`;
- `pause()`, `resume()`, or `idle()`.

Do not add empty placeholder methods “for future use”. A declared method is an API commitment; future API should be introduced only with a concrete requirement, validation rules, and tests.

## 5. Future configuration policy

V1 deliberately exposes no runtime configuration interface.

If a concrete product requirement later calls for runtime-adjustable settings, prefer one validated method such as:

```python
video.configure(jpeg_quality=50)
```

over a growing family of single-purpose methods. Add a convenience `set_xxx()` only if real usage shows that one parameter needs frequent standalone updates. A readback method such as `get_config()` should be added only if callers need it.

Potential future parameter groups:

1. **Hardware binding:** `spi_bus`, `cs_pin`. Not ordinary runtime settings. Revisit only when another supported hardware target is implemented.
2. **Advanced transport settings:** baud rate, payload size, maximum compressed frame size. These affect receiver compatibility or resource constraints and require explicit validation.
3. **User-facing image setting:** JPEG quality. This is the most plausible first candidate for runtime configuration.

If configuration grows complex, consider a dedicated `VideoConfig` object. Do not introduce it pre-emptively.

## 6. Configuration is not control

Configuration describes how a frame is encoded or transported. Product control describes whether/when the application calls the transport.

For example, the product may decide:

```python
if video_enabled:
    video.send_frame(img)
```

The library must not add `pause()` or `resume()` to represent product state. Pausing transmission also need not stop camera capture or vision processing.

## 7. Third-party installation and use

For a normal OpenMV filesystem, copy:

`openmv/core/video/video_core.py` -> `/lib/video_core.py`

Then use the stable import path:

```python
from video_core import VideoTransmitter

video = VideoTransmitter()
# In the application's existing loop:
# img = sensor.snapshot()
# video.send_frame(img)
```

Consumers should not import from repository-internal paths such as `openmv.core.video.video_core`. The file/module name `video_core.py` is the library-facing entry point.

## 8. Host-side tests

From the repository root, with CPython 3.9+:

```bash
python -m unittest discover -s tests -p "test_video_core.py" -v
```

The tests stub `pyb.SPI`, `pyb.Pin`, and MicroPython's `ustruct`/time extensions. They cover API shape, default hardware arguments, SPI V2 header and padding, multi-packet frames, compression/size failures, SPI errors, CS recovery, and statistics.

Host tests are a static/contract check only. They cannot validate actual OpenMV MicroPython behavior, electrical timing, ESP32 SPI-slave reception, frame reassembly, or end-to-end video playback. Those require hardware verification and must be recorded separately in `docs/migration-status.md`.
