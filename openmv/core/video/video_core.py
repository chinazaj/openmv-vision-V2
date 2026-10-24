# -*- coding: utf-8 -*-
"""Standalone OpenMV JPEG-over-SPI V2 video transport core.

The public V1 API is intentionally small:
    video = VideoTransmitter()
    video.send_frame(img)
    video.get_stats()
    video.reset_stats()

Camera capture, application state, and decisions about when to transmit belong
to the product/application, not this module.
"""

import time
import ustruct
from pyb import SPI, Pin


class VideoTransmitter:
    """Transmit OpenMV images as JPEG frames over the SPI V2 protocol."""

    # Fixed hardware target for the V1 product.
    SPI_BUS = 2
    CS_PIN = "P3"

    # SPI V2 wire-protocol constants. Do not change without coordinated
    # compatibility work on the ESP32 receiver.
    SPI_MAGIC = 0x55AA
    SPI_VERSION = 2
    SPI_TYPE_FRAME = 1
    SPI_HEADER_FMT = "<HBBHHHHIH"
    SPI_HEADER_SIZE = ustruct.calcsize(SPI_HEADER_FMT)

    # Internal V1 defaults. They are deliberately not constructor parameters.
    DEFAULT_BAUDRATE = 4000000
    DEFAULT_PAYLOAD_SIZE = 4096
    DEFAULT_MAX_FRAME_SIZE = 48 * 1024
    DEFAULT_JPEG_QUALITY = 40


    def __init__(self):
        self.spi = SPI(
            self.SPI_BUS,
            SPI.MASTER,
            baudrate=self.DEFAULT_BAUDRATE,
            polarity=0,
            phase=0,
        )
        self.cs = Pin(self.CS_PIN, Pin.OUT_PP)
        self.cs.high()

        self._payload_size = self.DEFAULT_PAYLOAD_SIZE
        self._max_frame_size = self.DEFAULT_MAX_FRAME_SIZE
        self._jpeg_quality = self.DEFAULT_JPEG_QUALITY

        self._frame_id = 0
        self._sent_frames = 0
        self._dropped_frames = 0
        self._last_frame_size = 0
        self._last_send_ms = 0
        self._total_send_ms = 0

    @staticmethod
    def _crc16_ccitt(data):
        """Return CRC16-CCITT used by SPI V2 frames."""
        crc = 0xFFFF
        for value in data:
            crc ^= value << 8
            for _ in range(8):
                if crc & 0x8000:
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc = (crc << 1) & 0xFFFF
        return crc

    def _send_packet(
        self,
        frame_id,
        packet_idx,
        total_packets,
        frame_size,
        frame_crc,
        payload,
    ):
        """Send one SPI V2 packet in exactly one CS-low transaction."""
        payload_len = len(payload)
        header = ustruct.pack(
            self.SPI_HEADER_FMT,
            self.SPI_MAGIC,
            self.SPI_VERSION,
            self.SPI_TYPE_FRAME,
            frame_id,
            packet_idx,
            total_packets,
            payload_len,
            frame_size,
            frame_crc,
        )

        pad_len = (4 - ((len(header) + payload_len) & 0x03)) & 0x03
        packet = header + payload
        if pad_len:
            packet += b"\x00" * pad_len

        self.cs.low()
        try:
            self.spi.send(packet)
        except Exception:
            return False
        finally:
            self.cs.high()

        return True

    def send_frame(self, img):
        """Compress and transmit one image.

        Returns True if every packet was accepted by the OpenMV SPI driver.
        Returns False if compression, frame-size validation, or a packet send
        fails. Failed frames are dropped rather than queued. True does not
        confirm that the ESP32 has received or validated the complete frame.
        """
        start_ms = time.ticks_ms()

        try:
            img_c = img.compress(quality=self._jpeg_quality)
            data = bytes(img_c)
        except Exception:
            self._dropped_frames += 1
            return False

        frame_size = len(data)
        self._last_frame_size = frame_size
        if frame_size <= 0 or frame_size > self._max_frame_size:
            self._dropped_frames += 1
            return False

        frame_id = self._frame_id
        self._frame_id = (self._frame_id + 1) & 0xFFFF
        frame_crc = self._crc16_ccitt(data)
        total_packets = (
            frame_size + self._payload_size - 1
        ) // self._payload_size

        for packet_idx in range(total_packets):
            start = packet_idx * self._payload_size
            end = min(start + self._payload_size, frame_size)
            payload = data[start:end]
            if not self._send_packet(
                frame_id,
                packet_idx,
                total_packets,
                frame_size,
                frame_crc,
                payload,
            ):
                self._dropped_frames += 1
                return False

        self._sent_frames += 1
        self._last_send_ms = time.ticks_diff(time.ticks_ms(), start_ms)
        self._total_send_ms += self._last_send_ms
        return True

    def get_stats(self):
        """Return a snapshot of runtime transmission counters."""
        return {
            "frame_id": self._frame_id,
            "sent_frames": self._sent_frames,
            "dropped_frames": self._dropped_frames,
            "last_frame_size": self._last_frame_size,
            "last_send_ms": self._last_send_ms,
            "total_send_ms": self._total_send_ms,
        }

    def reset_stats(self):
        """Reset counters without resetting the frame sequence."""
        self._sent_frames = 0
        self._dropped_frames = 0
        self._last_frame_size = 0
        self._total_send_ms = 0
