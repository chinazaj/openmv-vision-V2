# -*- coding: utf-8 -*-
"""Non-blocking dual-servo action feature migrated from V1."""

import time
from pyb import Servo


class DualServoController:
    def __init__(self, s1_id=1, s2_id=2):
        self.s1 = Servo(s1_id)
        self.s2 = Servo(s2_id)
        self.active = False
        self.action_id = 0
        self.return_time = 0

    def set_angle(self, servo_id, angle):
        angle = max(0, min(180, angle))
        if servo_id == 1:
            self.s1.angle(angle)
        elif servo_id == 2:
            self.s2.angle(angle)

    def reset_all(self, s1_mid, s2_mid):
        self.set_angle(1, s1_mid)
        self.set_angle(2, s2_mid)

    def start_action(self, action_id, servo_cfg, delay_ms=1200):
        if self.active:
            return False
        s1_left, s1_mid, s1_right = servo_cfg["s1"]
        s2_left, s2_mid, s2_right = servo_cfg["s2"]
        if action_id == 1:
            self.set_angle(1, s1_left)
        elif action_id == 2:
            self.set_angle(1, s1_right)
        elif action_id == 3:
            self.set_angle(2, s2_left)
        elif action_id == 4:
            self.set_angle(2, s2_right)
        else:
            return False
        self.action_id = action_id
        self.return_time = time.ticks_add(time.ticks_ms(), delay_ms)
        self.active = True
        return True

    def update(self, servo_cfg):
        if not self.active:
            return
        if time.ticks_diff(time.ticks_ms(), self.return_time) >= 0:
            s1_mid = servo_cfg["s1"][1]
            s2_mid = servo_cfg["s2"][1]
            if self.action_id in (1, 2):
                self.set_angle(1, s1_mid)
            elif self.action_id in (3, 4):
                self.set_angle(2, s2_mid)
            self.active = False
            self.action_id = 0
