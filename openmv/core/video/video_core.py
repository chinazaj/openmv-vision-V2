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
    DEFAULT_BAUDRATE = 2000000
    DEFAULT_PAYLOAD_SIZE = 4096
    DEFAULT_MAX_FRAME_SIZE = 48 * 1024
    DEFAULT_JPEG_QUALITY = 40
    DIAGNOSTIC_INTERVAL_MS = 5000
    # Must match ESP32 SPI_DIAG_REV in spi_video_receiver.cpp.
    SPI_DIAG_REV = "20261025A"

    def __init__(self):
        self._tx_packet_id = 0
        self.spi = SPI(
            self.SPI_BUS,
            SPI.MASTER,
            baudrate=self.DEFAULT_BAUDRATE,
            polarity=0,
            phase=1,
        )
        self.cs = Pin(self.CS_PIN, Pin.OUT_PP)
        self.cs.high()

        self._payload_size = self.DEFAULT_PAYLOAD_SIZE
        self._max_frame_size = self.DEFAULT_MAX_FRAME_SIZE
        self._jpeg_quality = self.DEFAULT_JPEG_QUALITY

        self._frame_id = 0
        self._sent_frames = 0
        self._dropped_frames = 0
        self._sent_packets = 0
        self._packet_send_errors = 0
        self._compress_errors = 0
        self._convert_errors = 0
        self._frame_length_errors = 0
        self._crc_errors = 0
        self._other_frame_errors = 0
        self._last_error_stage = "none"
        self._last_error = "none"
        self._last_frame_size = 0
        self._last_frame_packet_count = 0
        self._last_packet_len = 0
        self._last_packet_frame_id = 0
        self._last_packet_idx = 0
        self._last_packet_total = 0
        self._last_packet_payload_len = 0
        self._last_packet_send_ms = 0
        self._last_tx_packet_id = 0
        self._last_spi_call_us = 0
        self._max_packet_send_ms = 0
        self._last_send_ms = 0
        self._total_send_ms = 0
        self._diagnostic_last_ms = time.ticks_ms()
        print("[SPI DIAG BOOT] side=OPENMV diag_rev={} spi_bus={} spi_hz={} spi_mode=1 cpol=0 cpha=1 cs_pin={} payload_max={} header_size={} packet_align=4".format(
            self.SPI_DIAG_REV, self.SPI_BUS, self.DEFAULT_BAUDRATE, self.CS_PIN,
            self._payload_size, self.SPI_HEADER_SIZE
        ))

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

    def _print_diagnostics(self):
        """Print periodic SPI transport diagnostics to the OpenMV console."""
        now_ms = time.ticks_ms()
        if time.ticks_diff(now_ms, self._diagnostic_last_ms) < self.DIAGNOSTIC_INTERVAL_MS:
            return
        self._diagnostic_last_ms = now_ms

        avg_send_ms = 0
        if self._sent_frames:
            avg_send_ms = self._total_send_ms // self._sent_frames

        print(
            "[OPENMV SPI] frame_id={} sent_frames={} dropped_frames={} "
            "sent_packets={} send_errors={}".format(
                self._frame_id,
                self._sent_frames,
                self._dropped_frames,
                self._sent_packets,
                self._packet_send_errors,
            )
        )
        print(
            "[OPENMV SPI] frame: size={} packets={} last_send_ms={} avg_send_ms={}".format(
                self._last_frame_size,
                self._last_frame_packet_count,
                self._last_send_ms,
                avg_send_ms,
            )
        )
        print(
            "[OPENMV SPI] packet: tx_id={} len={} frame_id={} idx={}/{} payload={} "
            "spi_call_us={} send_ms={} max_send_ms={}".format(
                self._last_tx_packet_id,
                self._last_packet_len,
                self._last_packet_frame_id,
                self._last_packet_idx,
                self._last_packet_total,
                self._last_packet_payload_len,
                self._last_spi_call_us,
                self._last_packet_send_ms,
                self._max_packet_send_ms,
            )
        )
        print(
            "[OPENMV SPI] errors: compress={} convert={} frame_length={} crc={} other={} "
            "last_stage={} last_error={}".format(
                self._compress_errors,
                self._convert_errors,
                self._frame_length_errors,
                self._crc_errors,
                self._other_frame_errors,
                self._last_error_stage,
                self._last_error,
            )
        )

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

        packet = bytearray(header)
        packet.extend(payload)
        padding = (-len(packet)) & 3
        if padding:
            packet.extend(b"\x00" * padding)

        tx_packet_id = self._tx_packet_id
        self._tx_packet_id = (self._tx_packet_id + 1) & 0xFFFFFFFF
        # Do not print or format diagnostic payload dumps in the CS-low path.
        # Console I/O can block for tens of milliseconds and perturb SPI cadence.
        send_start_ms = time.ticks_ms()
        send_start_us = time.ticks_us()
        try:
            self.cs.low()
            self.spi.send(packet)
            send_done_us = time.ticks_us()
            self.cs.high()
        except Exception:
            self.cs.high()
            self._packet_send_errors += 1
            # Error reporting is intentionally deferred to periodic diagnostics.
            raise
        spi_call_us = time.ticks_diff(send_done_us, send_start_us)
        send_ms = time.ticks_diff(time.ticks_ms(), send_start_ms)

        self._sent_packets += 1
        self._last_packet_len = len(packet)
        self._last_packet_frame_id = frame_id
        self._last_packet_idx = packet_idx
        self._last_packet_total = total_packets
        self._last_packet_payload_len = payload_len
        self._last_packet_send_ms = send_ms
        self._last_tx_packet_id = tx_packet_id
        self._last_spi_call_us = spi_call_us
        if send_ms > self._max_packet_send_ms:
            self._max_packet_send_ms = send_ms

    def send_frame(self, image):
        """Compress and transmit one image as a sequence of SPI V2 packets."""
        frame_start_ms = time.ticks_ms()
        stage = "compress"
        try:
            compressed = image.compress(quality=self._jpeg_quality)

            stage = "convert"
            jpeg = bytes(compressed)

            stage = "frame_length"
            frame_size = len(jpeg)
            self._last_frame_size = frame_size
            if frame_size <= 0:
                self._frame_length_errors += 1
                self._dropped_frames += 1
                self._last_error_stage = "frame_length"
                self._last_error = "compressed frame has zero length"
                self._print_diagnostics()
                return False
            if frame_size > self._max_frame_size:
                self._frame_length_errors += 1
                self._dropped_frames += 1
                self._last_error_stage = "frame_length"
                self._last_error = "frame size {} exceeds limit {}".format(
                    frame_size, self._max_frame_size
                )
                self._print_diagnostics()
                return False

            stage = "crc"
            frame_crc = self._crc16_ccitt(jpeg)
            total_packets = (frame_size + self._payload_size - 1) // self._payload_size
            frame_id = self._frame_id
            self._frame_id = (self._frame_id + 1) & 0xFFFF

            stage = "packet_send"
            for packet_idx in range(total_packets):
                start = packet_idx * self._payload_size
                end = min(start + self._payload_size, frame_size)
                self._send_packet(
                    frame_id,
                    packet_idx,
                    total_packets,
                    frame_size,
                    frame_crc,
                    jpeg[start:end],
                )

            self._sent_frames += 1
            self._last_frame_size = frame_size
            self._last_frame_packet_count = total_packets
            self._last_send_ms = time.ticks_diff(time.ticks_ms(), frame_start_ms)
            self._total_send_ms += self._last_send_ms
            self._last_error_stage = "none"
            self._last_error = "none"
            self._print_diagnostics()
            return True
        except Exception as exc:
            self._dropped_frames += 1
            self._last_error_stage = stage
            try:
                self._last_error = repr(exc)
            except Exception:
                self._last_error = "exception text unavailable"
            if stage == "compress":
                self._compress_errors += 1
            elif stage == "convert":
                self._convert_errors += 1
            elif stage == "frame_length":
                self._frame_length_errors += 1
            elif stage == "crc":
                self._crc_errors += 1
            else:
                self._other_frame_errors += 1
            self._print_diagnostics()
            return False

    def get_stats(self):
        """Return a snapshot of current transport statistics."""
        return {
            "frame_id": self._frame_id,
            "sent_frames": self._sent_frames,
            "dropped_frames": self._dropped_frames,
            "sent_packets": self._sent_packets,
            "send_errors": self._packet_send_errors,
            "compress_errors": self._compress_errors,
            "convert_errors": self._convert_errors,
            "frame_length_errors": self._frame_length_errors,
            "crc_errors": self._crc_errors,
            "other_frame_errors": self._other_frame_errors,
            "last_error_stage": self._last_error_stage,
            "last_error": self._last_error,
            "last_frame_size": self._last_frame_size,
            "last_frame_packet_count": self._last_frame_packet_count,
            "last_packet_len": self._last_packet_len,
            "last_packet_frame_id": self._last_packet_frame_id,
            "last_packet_idx": self._last_packet_idx,
            "last_packet_total": self._last_packet_total,
            "last_packet_payload_len": self._last_packet_payload_len,
            "last_packet_send_ms": self._last_packet_send_ms,
            "max_packet_send_ms": self._max_packet_send_ms,
            "last_send_ms": self._last_send_ms,
            "total_send_ms": self._total_send_ms,
        }

    def reset_stats(self):
        """Reset runtime transport statistics while preserving configuration."""
        self._frame_id = 0
        self._sent_frames = 0
        self._dropped_frames = 0
        self._sent_packets = 0
        self._packet_send_errors = 0
        self._compress_errors = 0
        self._convert_errors = 0
        self._frame_length_errors = 0
        self._crc_errors = 0
        self._other_frame_errors = 0
        self._last_error_stage = "none"
        self._last_error = "none"
        self._last_frame_size = 0
        self._last_frame_packet_count = 0
        self._last_packet_len = 0
        self._last_packet_frame_id = 0
        self._last_packet_idx = 0
        self._last_packet_total = 0
        self._last_packet_payload_len = 0
        self._last_packet_send_ms = 0
        self._max_packet_send_ms = 0
        self._last_send_ms = 0
        self._total_send_ms = 0
        self._diagnostic_last_ms = time.ticks_ms()
