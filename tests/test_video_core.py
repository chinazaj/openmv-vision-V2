# -*- coding: utf-8 -*-
"""Host-side contract tests for OpenMV Video Core.

Run from the repository root:
    python -m unittest discover -s tests -p "test_video_core.py" -v

These tests use hardware stubs. They validate the Python-side API and packet
construction, but do not replace a test on the target OpenMV/ESP32 hardware.
"""

import importlib
import inspect
import pathlib
import struct
import sys
import time
import types
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_DIR = ROOT / "openmv" / "core" / "video"


class FakePin:
    OUT_PP = 1

    def __init__(self, name, mode):
        self.name = name
        self.mode = mode
        self.level = None

    def high(self):
        self.level = 1

    def low(self):
        self.level = 0


class FakeSPI:
    MASTER = 0
    instances = []
    fail_send_at = None

    def __init__(self, bus, mode, **kwargs):
        self.bus = bus
        self.mode = mode
        self.kwargs = kwargs
        self.sent_packets = []
        FakeSPI.instances.append(self)

    def send(self, packet):
        if (FakeSPI.fail_send_at is not None and
                len(self.sent_packets) == FakeSPI.fail_send_at):
            raise OSError("simulated SPI failure")
        self.sent_packets.append(packet)


def load_module():
    FakeSPI.instances = []
    FakeSPI.fail_send_at = None
    pyb_stub = types.ModuleType("pyb")
    pyb_stub.SPI = FakeSPI
    pyb_stub.Pin = FakePin
    ustruct_stub = types.ModuleType("ustruct")
    ustruct_stub.pack = struct.pack
    ustruct_stub.unpack = struct.unpack
    ustruct_stub.calcsize = struct.calcsize

    sys.modules["pyb"] = pyb_stub
    sys.modules["ustruct"] = ustruct_stub
    sys.path.insert(0, str(MODULE_DIR))
    if "video_core" in sys.modules:
        del sys.modules["video_core"]
    module = importlib.import_module("video_core")

    # MicroPython supplies these functions on its time module; CPython does not.
    time.ticks_ms = lambda: 100
    time.ticks_diff = lambda end, start: end - start
    return module


class FakeImage:
    def __init__(self, data=b"jpeg-frame", fail=False):
        self.data = data
        self.fail = fail
        self.quality_seen = None

    def compress(self, quality):
        self.quality_seen = quality
        if self.fail:
            raise ValueError("simulated compression failure")
        return self.data


class VideoCoreContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_module()
        cls.VideoTransmitter = cls.module.VideoTransmitter

    def setUp(self):
        FakeSPI.instances = []
        FakeSPI.fail_send_at = None
        self.video = self.VideoTransmitter()
        self.spi = FakeSPI.instances[-1]

    def test_v1_constructor_has_no_public_configuration_parameters(self):
        signature = inspect.signature(self.VideoTransmitter)
        self.assertEqual(list(signature.parameters), [])
        self.assertEqual(self.spi.bus, 2)
        self.assertEqual(self.spi.kwargs["baudrate"], 4000000)
        self.assertEqual(self.spi.kwargs["polarity"], 0)
        self.assertEqual(self.spi.kwargs["phase"], 0)
        self.assertEqual(self.video.cs.name, "P3")
        self.assertEqual(self.video.cs.level, 1)

    def test_send_frame_uses_default_quality_and_builds_v2_packet(self):
        image = FakeImage(b"jpeg-frame")
        self.assertTrue(self.video.send_frame(image))
        self.assertEqual(image.quality_seen, 40)
        self.assertEqual(len(self.spi.sent_packets), 1)

        packet = self.spi.sent_packets[0]
        header = struct.unpack("<HBBHHHHIH", packet[:18])
        magic, version, packet_type, frame_id, packet_idx, count, payload_len, frame_size, crc = header
        self.assertEqual((magic, version, packet_type), (0x55AA, 2, 1))
        self.assertEqual((frame_id, packet_idx, count), (0, 0, 1))
        self.assertEqual(payload_len, len(b"jpeg-frame"))
        self.assertEqual(frame_size, len(b"jpeg-frame"))
        self.assertEqual(crc, self.VideoTransmitter._crc16_ccitt(b"jpeg-frame"))
        self.assertEqual(packet[18:18 + payload_len], b"jpeg-frame")
        self.assertEqual(len(packet) % 4, 0)
        self.assertEqual(self.video.cs.level, 1)
        self.assertEqual(self.video.get_stats()["sent_frames"], 1)

    def test_large_frame_is_split_into_multiple_packets(self):
        data = b"x" * 5000
        self.assertTrue(self.video.send_frame(FakeImage(data)))
        self.assertEqual(len(self.spi.sent_packets), 2)
        first = struct.unpack("<HBBHHHHIH", self.spi.sent_packets[0][:18])
        second = struct.unpack("<HBBHHHHIH", self.spi.sent_packets[1][:18])
        self.assertEqual(first[5], 2)
        self.assertEqual(second[5], 2)
        self.assertEqual(first[4], 0)
        self.assertEqual(second[4], 1)
        self.assertEqual(first[6], 4096)
        self.assertEqual(second[6], 904)

    def test_compression_failure_drops_frame(self):
        self.assertFalse(self.video.send_frame(FakeImage(fail=True)))
        stats = self.video.get_stats()
        self.assertEqual(stats["dropped_frames"], 1)
        self.assertEqual(stats["sent_frames"], 0)

    def test_empty_or_oversize_frame_is_rejected(self):
        self.assertFalse(self.video.send_frame(FakeImage(b"")))
        self.video._max_frame_size = 4
        self.assertFalse(self.video.send_frame(FakeImage(b"12345")))
        self.assertEqual(self.video.get_stats()["dropped_frames"], 2)
        self.assertEqual(self.spi.sent_packets, [])

    def test_spi_failure_drops_frame_and_restores_cs_high(self):
        FakeSPI.fail_send_at = 0
        self.assertFalse(self.video.send_frame(FakeImage(b"jpeg")))
        self.assertEqual(self.video.get_stats()["dropped_frames"], 1)
        self.assertEqual(self.video.cs.level, 1)

    def test_reset_stats_does_not_reset_frame_sequence(self):
        self.assertTrue(self.video.send_frame(FakeImage(b"one")))
        self.assertEqual(self.video.get_stats()["frame_id"], 1)
        self.video.reset_stats()
        stats = self.video.get_stats()
        self.assertEqual(stats["frame_id"], 1)
        self.assertEqual(stats["sent_frames"], 0)
        self.assertEqual(stats["dropped_frames"], 0)
        self.assertEqual(stats["last_frame_size"], 0)
        self.assertEqual(stats["last_send_ms"], 0)
        self.assertEqual(stats["total_send_ms"], 0)

    def test_no_placeholder_or_business_control_api(self):
        for name in ("configure", "get_config", "pause", "resume", "idle", "send_packet"):
            self.assertFalse(hasattr(self.video, name), name)


if __name__ == "__main__":
    unittest.main()
