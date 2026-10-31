# -*- coding: utf-8 -*-
"""OpenMV device configuration core.

This module owns config.json persistence and validation. It deliberately does
not own Wi-Fi settings or application-specific camera behavior.
"""

import ujson


CONTROL_VIDEO_ON_CODE = "MVTP_RESERVED_VIDEO_ON"
CONTROL_VIDEO_OFF_CODE = "MVTP_RESERVED_VIDEO_OFF"


class ConfigManager:
    """Compatibility-preserving config.json manager migrated from V1."""

    def __init__(self, filename="config.json", debug=False):
        self.filename = filename
        self.debug = debug
        self.video_enabled = False
        self.config = {
            "schema_version": 2,
            "video": {"enabled": False},
            "analysis": {"mode": "qr", "targets": {
                "color": [{"enabled": False, "name": "color-%d" % (i + 1), "hex": "#FF0000", "tolerance": 18} for i in range(4)],
                "shape": [{"enabled": False, "name": "shape-%d" % (i + 1), "type": "circle"} for i in range(4)],
                "qr": [{"enabled": False, "name": "qr-%d" % (i + 1), "payload": ""} for i in range(4)]
            }},
            "control_qr": {"enabled": True, "video_on_code": CONTROL_VIDEO_ON_CODE,
                           "video_off_code": CONTROL_VIDEO_OFF_CODE},
            "servo": {"s1": [0, 90, 180], "s2": [0, 90, 180]},
            "bind": {"color": [1, 2, 3, 4], "shape": [1, 2, 3, 4], "qr": [1, 2, 3, 4]},
            "qr_type": "REGION",
            "sort_delay_ms": 1200
        }
        self.color_thresholds = [
            (30, 80, 15, 127, 15, 127),
            (40, 85, -10, 50, 20, 127),
            (50, 95, -30, 10, 30, 127),
            (30, 80, -128, -10, -128, 0)
        ]
        self.load()

    def _log(self, *args):
        if self.debug:
            print(*args)

    def _clone_config(self, config):
        # A JSON round trip provides a deep copy on MicroPython/ujson builds
        # that do not expose copy.deepcopy.
        return ujson.loads(ujson.dumps(config))

    def load(self):
        try:
            with open(self.filename, "r") as f:
                loaded = ujson.load(f)
            if not isinstance(loaded, dict):
                raise ValueError("config root must be an object")
            self.config.update(loaded)
            old_version = int(loaded.get("schema_version", 1))
            if old_version < 2 and "servo" in loaded:
                old_servo = self.config.get("servo", {})
                for servo_id in ("s1", "s2"):
                    angles = old_servo.get(servo_id, [90, 0, 180])
                    if isinstance(angles, list) and len(angles) == 3:
                        old_servo[servo_id] = [angles[1], angles[0], angles[2]]
                self.config["servo"] = old_servo
            self.config["schema_version"] = 2
            if "analysis" not in self.config:
                old_en = self.config.get("en", {})
                mode = "qr" if old_en.get("qr", 1) else ("color" if old_en.get("color", 0) else ("shape" if old_en.get("shape", 0) else "none"))
                self.config["analysis"] = {"mode": mode, "targets": {"color": [], "shape": [], "qr": []}}
            self.config.setdefault("video", {"enabled": False})
            self.config.setdefault("control_qr", {"enabled": True, "video_on_code": CONTROL_VIDEO_ON_CODE,
                                                    "video_off_code": CONTROL_VIDEO_OFF_CODE})
            self.validate(self.config)
            self.video_enabled = bool(self.config.get("video", {}).get("enabled", False))
        except Exception as exc:
            self._log("[CONFIG] Load failed; using safe defaults:", exc)
            self.video_enabled = bool(self.config.get("video", {}).get("enabled", False))
            self.save()

    def validate(self, candidate):
        analysis = candidate.get("analysis", {})
        if not isinstance(analysis, dict):
            raise ValueError("analysis must be an object")
        if analysis.get("mode", "qr") not in ("none", "qr", "color", "shape"):
            raise ValueError("analysis.mode must be none/qr/color/shape")
        targets = analysis.get("targets", {})
        if not isinstance(targets, dict):
            raise ValueError("analysis.targets must be an object")
        for kind in ("color", "shape", "qr"):
            values = targets.get(kind, [])
            if not isinstance(values, list) or len(values) > 4:
                raise ValueError("%s targets must contain at most 4 slots" % kind)
            while len(values) < 4:
                if kind == "color":
                    values.append({"enabled": False, "name": "color-%d" % (len(values) + 1), "hex": "#FF0000", "tolerance": 18})
                elif kind == "shape":
                    values.append({"enabled": False, "name": "shape-%d" % (len(values) + 1), "type": "circle"})
                else:
                    values.append({"enabled": False, "name": "qr-%d" % (len(values) + 1), "payload": ""})
            targets[kind] = values
            for target in values:
                if not isinstance(target, dict):
                    raise ValueError("%s slot must be an object" % kind)
                default_enabled = bool(target.get("hex", "")) if kind == "color" else (bool(target.get("payload", "")) if kind == "qr" else True)
                target.setdefault("enabled", default_enabled)
                if not isinstance(target.get("enabled"), bool):
                    raise ValueError("%s enabled must be boolean" % kind)
        analysis["targets"] = targets
        candidate["analysis"] = analysis
        for target in targets.get("color", []):
            color = target.get("hex", "#FF0000")
            if not isinstance(color, str) or len(color) != 7 or color[0] != "#":
                raise ValueError("color target hex must be #RRGGBB")
            int(color[1:], 16)
            target["tolerance"] = max(1, min(80, int(target.get("tolerance", 18))))
        for target in targets.get("shape", []):
            if target.get("type") not in ("circle", "rectangle"):
                raise ValueError("supported shapes are circle and rectangle")
        servo = candidate.get("servo", {})
        if not isinstance(servo, dict):
            raise ValueError("servo must be an object")
        for servo_id in ("s1", "s2"):
            angles = servo.get(servo_id, [0, 90, 180])
            if not isinstance(angles, list) or len(angles) != 3:
                raise ValueError("servo.%s must be [left, mid, right]" % servo_id)
            servo[servo_id] = [max(0, min(180, int(v))) for v in angles]
        candidate["servo"] = servo
        video = candidate.get("video", {})
        if not isinstance(video, dict):
            raise ValueError("video must be an object")
        if not isinstance(video.get("enabled", False), bool):
            raise ValueError("video.enabled must be boolean")

    def update_item(self, item, index, value):
        if not isinstance(value, dict):
            raise ValueError("config item value must be an object")
        candidate = self._clone_config(self.config)
        if item == "analysis":
            mode = value.get("mode")
            if mode not in ("none", "qr", "color", "shape"):
                raise ValueError("analysis.mode must be none/qr/color/shape")
            candidate["analysis"]["mode"] = mode
        elif item in ("color", "shape", "qr"):
            if not isinstance(index, int) or index < 0 or index >= 4:
                raise ValueError("config slot index must be 0..3")
            target = candidate["analysis"]["targets"][item][index]
            allowed = {"color": ("enabled", "hex", "tolerance"),
                       "shape": ("enabled", "type"),
                       "qr": ("enabled", "payload")}[item]
            for key in value:
                if key not in allowed:
                    raise ValueError("unsupported %s field: %s" % (item, key))
            target.update(value)
            target["name"] = "%s-%d" % (item, index + 1)
        elif item == "servo":
            if index not in (0, 1):
                raise ValueError("servo index must be 0..1")
            angles = [int(value.get("left")), int(value.get("mid")), int(value.get("right"))]
            candidate["servo"]["s%d" % (index + 1)] = [max(0, min(180, v)) for v in angles]
        elif item == "control_qr":
            allowed = ("enabled", "video_on_code", "video_off_code")
            for key in value:
                if key not in allowed:
                    raise ValueError("unsupported control_qr field: %s" % key)
            if "enabled" in value and not isinstance(value["enabled"], bool):
                raise ValueError("control_qr.enabled must be boolean")
            for key in ("video_on_code", "video_off_code"):
                if key in value and not isinstance(value[key], str):
                    raise ValueError("control QR codes must be strings")
            candidate["control_qr"].update(value)
        else:
            raise ValueError("unknown config item: %s" % item)
        candidate["schema_version"] = 2
        self.validate(candidate)
        previous = self.config
        self.config = candidate
        try:
            self.save()
        except Exception:
            self.config = previous
            raise
        self.video_enabled = bool(self.config.get("video", {}).get("enabled", False))
        return self.config

    def get_item(self, item, index=-1):
        cfg = self.config
        if item == "analysis":
            return {"mode": cfg.get("analysis", {}).get("mode", "qr")}
        if item in ("color", "shape", "qr"):
            if not isinstance(index, int) or index < 0 or index >= 4:
                raise ValueError("config slot index must be 0..3")
            return cfg["analysis"]["targets"][item][index]
        if item == "servo":
            if index not in (0, 1):
                raise ValueError("servo index must be 0..1")
            angles = cfg["servo"]["s%d" % (index + 1)]
            return {"left": angles[0], "mid": angles[1], "right": angles[2]}
        if item == "control_qr":
            return cfg.get("control_qr", {})
        raise ValueError("unknown config item: %s" % item)

    def save(self):
        with open(self.filename, "w") as f:
            ujson.dump(self.config, f)
        with open(self.filename, "r") as f:
            saved = ujson.load(f)
        if saved != self.config:
            raise ValueError("config.json read-back does not match in-memory config")

    def set_video_enabled(self, enabled):
        if not isinstance(enabled, bool):
            raise ValueError("video.enabled must be boolean")
        candidate = self._clone_config(self.config)
        candidate.setdefault("video", {})["enabled"] = enabled
        self.validate(candidate)
        previous = self.config
        previous_video_enabled = self.video_enabled
        self.config = candidate
        try:
            self.save()
        except Exception:
            self.config = previous
            self.video_enabled = previous_video_enabled
            raise
        self.video_enabled = enabled
        return enabled

    def update(self, new_cfg):
        if not isinstance(new_cfg, dict):
            raise ValueError("configuration update must be an object")
        candidate = self._clone_config(self.config)
        if isinstance(new_cfg.get("config"), dict):
            incoming = self._clone_config(new_cfg["config"])
            candidate.update(incoming)
        else:
            incoming = self._clone_config(new_cfg)
            candidate.update(incoming)
        candidate["schema_version"] = 2
        self.validate(candidate)
        previous = self.config
        previous_video_enabled = self.video_enabled
        self.config = candidate
        try:
            self.save()
        except Exception:
            self.config = previous
            self.video_enabled = previous_video_enabled
            raise
        self.video_enabled = bool(self.config.get("video", {}).get("enabled", False))
        return self.config
