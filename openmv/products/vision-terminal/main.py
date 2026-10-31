# -*- coding: utf-8 -*-
"""OpenMV Vision Terminal product entry point.

Deployment layout on the OpenMV filesystem:
  /main.py                         (this file)
  /core/config/config_core.py
  /core/uart/uart_core.py
  /core/video/video_core.py
  /features/servo/servo_feature.py
  /features/vision/vision_feature.py

The Video Terminal product remains a separate baseline. This product composes
the shared video core with UART, config, servo, and vision features.
"""

import sensor
import time

from core.config.config_core import ConfigManager
from core.uart.uart_core import UARTJsonlTransport
from core.video.video_core import VideoTransmitter
from features.servo.servo_feature import DualServoController
from features.vision.vision_feature import VisionDetector


BUILD_PROFILE = "DEV"
_native_print = print


def print(*args):
    if BUILD_PROFILE == "DEV":
        _native_print(*args)


VIDEO_ENABLED = False


def main():
    global VIDEO_ENABLED

    sensor.reset()
    sensor.set_pixformat(sensor.RGB565)
    sensor.set_framesize(sensor.QVGA)
    sensor.skip_frames(time=2000)

    cfg_mgr = ConfigManager(debug=(BUILD_PROFILE == "DEV"))
    VIDEO_ENABLED = cfg_mgr.video_enabled

    servos = DualServoController()
    detector = VisionDetector()
    video_tx = VideoTransmitter()
    comm = UARTJsonlTransport(
        uart_bus=3,
        baudrate=115200,
        rxbuf=8192,
        max_line_length=8192,
        debug=(BUILD_PROFILE == "DEV")
    )

    servo_cfg = cfg_mgr.config["servo"]
    servos.reset_all(servo_cfg["s1"][1], servo_cfg["s2"][1])

    def handle_command(cmd):
        global VIDEO_ENABLED
        command = cmd.get("cmd")

        if command in ("sync_config", "apply_config"):
            cfg_mgr.update(cmd)
            VIDEO_ENABLED = cfg_mgr.video_enabled
            current_servo_cfg = cfg_mgr.config["servo"]
            servos.reset_all(current_servo_cfg["s1"][1], current_servo_cfg["s2"][1])
            comm.send_json({
                "type": "resp",
                "status": "ok",
                "config_saved": True,
                "video_enabled": VIDEO_ENABLED
            })
            comm.send_json({
                "type": "config",
                "config": cfg_mgr.config,
                "video_enabled": VIDEO_ENABLED
            })

        elif command == "set_config_item":
            item = cmd.get("item")
            index = cmd.get("index", -1)
            cfg_mgr.update_item(item, index, cmd.get("value"))
            if item == "servo":
                current_servo_cfg = cfg_mgr.config["servo"]
                servos.reset_all(current_servo_cfg["s1"][1], current_servo_cfg["s2"][1])
            canonical_value = cfg_mgr.get_item(item, index)
            comm.send_json({
                "type": "resp",
                "status": "ok",
                "config_saved": True,
                "item": item,
                "index": index,
                "value": canonical_value
            })

        elif command == "get_config_item":
            item = cmd.get("item")
            index = cmd.get("index", -1)
            value = cfg_mgr.get_item(item, index)
            comm.send_json({
                "type": "config_item",
                "status": "ok",
                "item": item,
                "index": index,
                "value": value
            })

        elif command == "set_video":
            enabled = cmd.get("enabled")
            if isinstance(enabled, bool):
                cfg_mgr.set_video_enabled(enabled)
                VIDEO_ENABLED = cfg_mgr.video_enabled
                comm.send_json({
                    "type": "resp",
                    "status": "ok",
                    "video_enabled": VIDEO_ENABLED,
                    "runtime_control": True
                })
            else:
                comm.send_json({
                    "type": "resp",
                    "status": "error",
                    "reason": "enabled_must_be_bool"
                })

        elif command == "get_status":
            comm.send_json({
                "type": "status",
                "video_enabled": VIDEO_ENABLED,
                "analysis_mode": cfg_mgr.config.get("analysis", {}).get("mode", "qr"),
                "uart": dict(comm.stats)
            })

        elif command == "get_config":
            comm.send_json({
                "type": "config",
                "config": cfg_mgr.config,
                "video_enabled": VIDEO_ENABLED
            })

        else:
            print("[UART ERROR] Unknown/missing cmd: %r" % (command,))
            comm.send_json({
                "type": "resp",
                "status": "error",
                "reason": "unknown_command"
            })

    print("[BOOT] OpenMV Vision Terminal started")
    print("[BOOT] UART control: UART3, 115200 baud, 8N1")
    print("[BOOT] JSON line protocol uses LF terminator; VIDEO_ENABLED=%s" % VIDEO_ENABLED)
    print("[BOOT] Waiting for ESP32-C3 commands...")

    clock = time.clock()
    camera_error_reported = False

    while True:
        clock.tick()

        # Keep the existing once-per-loop communication schedule. Configuration
        # commands are infrequent and may take effect on the next loop.
        comm.poll(handle_command)
        servos.update(cfg_mgr.config["servo"])

        try:
            img = sensor.snapshot()
            camera_error_reported = False
        except Exception as exc:
            if not camera_error_reported:
                comm.send_json({
                    "type": "event",
                    "event": "error",
                    "severity": "error",
                    "code": "CAMERA_SNAPSHOT_FAILED"
                })
                camera_error_reported = True
            if BUILD_PROFILE == "DEV":
                print("[CAMERA ERROR] snapshot failed: %s" % exc)
            time.sleep_ms(100)
            continue

        result = detector.process(img, cfg_mgr)
        if (result.get("control_video") is not None and
                bool(result["control_video"]) != VIDEO_ENABLED):
            requested_video_state = bool(result["control_video"])
            try:
                cfg_mgr.set_video_enabled(requested_video_state)
                VIDEO_ENABLED = cfg_mgr.video_enabled
                comm.send_json({
                    "type": "event",
                    "event": "video_state_changed",
                    "video_enabled": VIDEO_ENABLED,
                    "source": "control_qr"
                })
            except Exception as exc:
                comm.send_json({
                    "type": "event",
                    "event": "error",
                    "severity": "error",
                    "code": "CONTROL_QR_CONFIG_SAVE_FAILED"
                })
                if BUILD_PROFILE == "DEV":
                    print("[CONFIG] Could not persist control QR video state:", exc)

        bind = cfg_mgr.config["bind"]
        servo_cfg = cfg_mgr.config["servo"]

        if not servos.active:
            if result["color_idx"] != -1:
                action_id = bind["color"][result["color_idx"]]
                comm.send_json({"type": "event", "det": "color", "act": action_id})
                servos.start_action(action_id, servo_cfg, cfg_mgr.config["sort_delay_ms"])
            elif result["shape_idx"] != -1:
                action_id = bind["shape"][result["shape_idx"]]
                comm.send_json({"type": "event", "det": "shape", "act": action_id})
                servos.start_action(action_id, servo_cfg, cfg_mgr.config["sort_delay_ms"])
            elif result["qr_idx"] != -1:
                action_id = bind["qr"][result["qr_idx"]]
                comm.send_json({"type": "event", "det": "qr", "act": action_id})
                servos.start_action(action_id, servo_cfg, cfg_mgr.config["sort_delay_ms"])

        if VIDEO_ENABLED:
            video_tx.send_frame(img)


main()
