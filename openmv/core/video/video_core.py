# -*- coding: utf-8 -*-
"""Standalone OpenMV JPEG-over-SPI video transport core.

This module intentionally has no dependency on sensor, UART, vision, config,
or application state. A caller supplies an OpenMV image object and decides
whether/when a frame should be transmitted.
"""

import time
import ustruct
from pyb import SPI, Pin


class VideoTransmitter:
    """Encode OpenMV images as JPEG and transmit them using SPI V2."""

    SPI_MAGIC = 0x55AA
    SPI_VERSION = 2
    SPI_TYPE_FRAME = 1
    SPI_HEADER_FMT = "<HBBHHHHIH"
    SPI_HEADER_SIZE = ustruct.calcsize(SPI_HEADER_FMT)

    DEFAULT_BAUDRATE = 4000000
    DEFAULT_PAYLOAD_SIZE = 4096
    DEFAULT_MAX_FRAME_SIZE = 48 * 1024
    DEFAULT_JPEG_QUALITY = 40

    def __init__(
        self,
        spi_bus=2,
        cs_pin="P3",
        baudrate=DEFAULT_BAUDRATE,
        payload_size=DEFAULT_PAYLOAD_SIZE,
        max_frame_size=DEFAULT_MAX_FRAME_SIZE,
        jpeg_quality=DEFAULT_JPEG_QUALITY,
    ):
        if payload_size <= 0:
            raise ValueError("payload_size must be positive")
        if max_frame_size <= 0:
            raise ValueError("max_frame_size must be positive")
        if jpeg_quality < 1 or jpeg_quality > 100:
            raise ValueError("jpeg_quality must be 1..100")

        self.spi = SPI(
            spi_bus,
            SPI.MASTER,
            baudrate=baudrate,
            polarity=0,
            phase=0,
        )
        self.cs = Pin(cs_pin, Pin.OUT_PP)
        self.cs.high()

        self.payload_size = int(payload_size)
        self.max_frame_size = int(max_frame_size)
        self.jpeg_quality = int(jpeg_quality)

        self.frame_id = 0
        self.sent_frames = 0
        self.dropped_frames = 0
        self.last_frame_size = 0
        self.last_send_ms = 0
        self.total_send_ms = 0

    @staticmethod
    def crc16_ccitt(data):
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

    def send_packet(
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

    def send_frame(self, img, quality=None):
        """Compress and transmit one OpenMV image.

        Returns True only when every packet of the frame was accepted by the
        OpenMV SPI driver. A failed frame is dropped rather than queued.
        """
        start_ms = time.ticks_ms()
        if quality is None:
            quality = self.jpeg_quality

        try:
            img_c = img.compress(quality=quality)
            data = bytes(img_c)
        except Exception:
            self.dropped_frames += 1
            return False

        frame_size = len(data)
        self.last_frame_size = frame_size
        if frame_size <= 0 or frame_size > self.max_frame_size:
            self.dropped_frames += 1
            return False

        frame_id = self.frame_id
        self.frame_id = (self.frame_id + 1) & 0xFFFF
        frame_crc = self.crc16_ccitt(data)
        total_packets = (
            frame_size + self.payload_size - 1
        ) // self.payload_size

        for packet_idx in range(total_packets):
            start = packet_idx * self.payload_size
            end = min(start + self.payload_size, frame_size)
            payload = data[start:end]
            if not self.send_packet(
                frame_id,
                packet_idx,
                total_packets,
                frame_size,
                frame_crc,
                payload,
            ):
                self.dropped_frames += 1
                return False

        self.sent_frames += 1
        self.last_send_ms = time.ticks_diff(time.ticks_ms(), start_ms)
        self.total_send_ms += self.last_send_ms
        return True

    def get_stats(self):
        """Return a small immutable-style statistics dictionary."""
        return {
            "frame_id": self.frame_id,
            "sent_frames": self.sent_frames,
            "dropped_frames": self.dropped_frames,
            "last_frame_size": self.last_frame_size,
            "last_send_ms": self.last_send_ms,
            "total_send_ms": self.total_send_ms,
        }

    def reset_stats(self):
        """Reset runtime counters without resetting the frame sequence."""
        self.sent_frames = 0
        self.dropped_frames = 0
        self.last_frame_size = 0
        self.last_send_ms = 0
        self.total_send_ms = 0

    def idle(self):
        """Return the CS line to the idle state.

        This is safe to call from an application's exception/finally handler.
        """
        self.cs.high()
