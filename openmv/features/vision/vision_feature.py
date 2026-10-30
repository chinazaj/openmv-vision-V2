# -*- coding: utf-8 -*-
"""Vision analysis feature migrated from the V1 OpenMV firmware.

This module contains QR, color, and shape recognition only. It does not own
camera capture, UART commands, configuration persistence, video transport,
or servo actuation.
"""

from ..core.config.config_core import (
    CONTROL_VIDEO_ON_CODE,
    CONTROL_VIDEO_OFF_CODE,
)


CONTROL_QR_SCAN_INTERVAL = 3


def hex_to_lab_threshold(hex_color, tolerance=18):
    """Convert an sRGB HEX color to an approximate CIELAB threshold tuple."""
    value = hex_color.lstrip("#")
    r = int(value[0:2], 16) / 255.0
    g = int(value[2:4], 16) / 255.0
    b = int(value[4:6], 16) / 255.0

    def linear(v):
        return ((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92

    r, g, b = linear(r), linear(g), linear(b)
    x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047
    y = r * 0.2126 + g * 0.7152 + b * 0.0722
    z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883

    def f(v):
        return v ** (1.0 / 3.0) if v > 0.008856 else 7.787 * v + 16.0 / 116.0

    fx, fy, fz = f(x), f(y), f(z)
    lab_l = 116.0 * fy - 16.0
    lab_a = 500.0 * (fx - fy)
    lab_b = 200.0 * (fy - fz)
    tol = max(5, min(40, int(tolerance)))
    return (
        max(0, int(lab_l - tol)), min(100, int(lab_l + tol)),
        max(-128, int(lab_a - tol)), min(127, int(lab_a + tol)),
        max(-128, int(lab_b - tol)), min(127, int(lab_b + tol))
    )


class VisionDetector:
    """Run the configured mutually-exclusive vision analysis mode."""

    def __init__(self, control_qr_scan_interval=CONTROL_QR_SCAN_INTERVAL):
        self.frame_counter = 0
        self.control_qr_scan_interval = control_qr_scan_interval

    def process(self, img, cfg_mgr):
        self.frame_counter += 1
        cfg = cfg_mgr.config
        analysis = cfg.get("analysis", {})
        mode = analysis.get("mode", "qr")
        targets = analysis.get("targets", {})
        result = {
            "color_idx": -1,
            "shape_idx": -1,
            "qr_idx": -1,
            "control_video": None
        }

        # Reserved video-control QR is scanned independently from business QR.
        qrs = []
        control_cfg = cfg.get("control_qr", {})
        if (self.frame_counter % self.control_qr_scan_interval == 1 and
                control_cfg.get("enabled", True)):
            qrs = img.find_qrcodes()
            for qr in qrs:
                payload = qr.payload()
                if payload == control_cfg.get("video_on_code", CONTROL_VIDEO_ON_CODE):
                    result["control_video"] = True
                    img.draw_rectangle(qr.rect(), color=(255, 0, 255))
                    break
                if payload == control_cfg.get("video_off_code", CONTROL_VIDEO_OFF_CODE):
                    result["control_video"] = False
                    img.draw_rectangle(qr.rect(), color=(255, 0, 255))
                    break

        if mode == "qr":
            on_code = control_cfg.get("video_on_code", CONTROL_VIDEO_ON_CODE)
            off_code = control_cfg.get("video_off_code", CONTROL_VIDEO_OFF_CODE)
            for qr in qrs:
                payload = qr.payload()
                if payload == on_code or payload == off_code:
                    continue
                img.draw_rectangle(qr.rect(), color=(0, 0, 255))
                for idx, target in enumerate(targets.get("qr", [])):
                    expected = target.get("payload", "")
                    if target.get("enabled", bool(expected)) and expected and payload == expected:
                        result["qr_idx"] = idx
                        break
                if result["qr_idx"] >= 0:
                    break

        elif mode == "color":
            for idx, target in enumerate(targets.get("color", [])):
                if not target.get("enabled", bool(target.get("hex", ""))):
                    continue
                threshold = hex_to_lab_threshold(
                    target.get("hex", "#FF0000"),
                    target.get("tolerance", 18)
                )
                blobs = img.find_blobs(
                    [threshold],
                    pixels_threshold=200,
                    area_threshold=200,
                    merge=True
                )
                if blobs:
                    result["color_idx"] = idx
                    img.draw_rectangle(blobs[0].rect(), color=(255, 0, 0))
                    break

        elif mode == "shape":
            for idx, target in enumerate(targets.get("shape", [])):
                if not target.get("enabled", True):
                    continue
                shape = target.get("type")
                if shape == "circle":
                    found = img.find_circles(
                        threshold=2000,
                        x_filter_margin=10,
                        y_filter_margin=10
                    )
                    if found:
                        circle = found[0]
                        result["shape_idx"] = idx
                        img.draw_circle(
                            circle.x(), circle.y(), circle.r(),
                            color=(0, 255, 0)
                        )
                        break
                elif shape == "rectangle":
                    found = img.find_rects(threshold=10000)
                    if found:
                        result["shape_idx"] = idx
                        img.draw_rectangle(found[0].rect(), color=(0, 255, 0))
                        break

        return result
