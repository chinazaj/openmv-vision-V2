# OpenMV Vision Terminal

This product composes the shared OpenMV Video Core with UART JSONL, device
configuration, servo control, and QR/color/shape analysis.

## OpenMV filesystem deployment

Copy the product entry point to the OpenMV filesystem root as `main.py`.
Copy the shared Core and Feature directories so the device filesystem is:

```text
/main.py
/core/
  __init__.py
  config/config_core.py
  uart/uart_core.py
  video/video_core.py
/features/
  __init__.py
  servo/servo_feature.py
  vision/vision_feature.py
/config.json   # created on first boot if absent
```

The OpenMV script search path must include the filesystem root. The product
uses MicroPython/OpenMV modules (`sensor`, `time`, `ujson`, `pyb`).

## Configuration scope

`config.json` is the authoritative OpenMV-side configuration. This product
migrates the V1 configuration contract, including:

- `schema_version`, `video.enabled`
- `analysis.mode` and four target slots each for `color`, `shape`, and `qr`
- `control_qr` reserved video on/off payloads
- `servo.s1/s2`, `bind.color/shape/qr`, `sort_delay_ms`, and `qr_type`

Wi-Fi credentials and ESP32 network configuration are intentionally excluded.
The ESP32 NVS vision cache is not a replacement for OpenMV `config.json`.

## Runtime contract

- UART3, 115200 baud, 8N1, UTF-8 JSONL terminated by LF.
- Video frames are sent through the existing `VideoTransmitter` in
  `core/video/video_core.py`; SPI V2 behavior is not reimplemented here.
- Analysis modes remain mutually exclusive: `none`, `qr`, `color`, `shape`.
- Reserved control QR scanning remains independent of business QR matching.
- Servo actions remain non-blocking and use the configured detection bindings.

This migration is not hardware-verified yet. Do not treat the product as a
release baseline until syntax/import checks, protocol regression checks, and
OpenMV + ESP32-C3 hardware validation have passed.
