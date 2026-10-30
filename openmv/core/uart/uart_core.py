# -*- coding: utf-8 -*-
"""UART JSONL transport core for OpenMV <-> ESP32-C3.

Wire contract: UART3, 115200 baud, 8N1, UTF-8 JSON objects terminated by LF.
Command semantics are owned by the product/application callback.
"""

import ujson
from pyb import UART


class UARTJsonlTransport:
    """Receive complete JSONL commands and transmit JSONL responses."""

    def __init__(self, uart_bus=3, baudrate=115200, rxbuf=8192,
                 max_line_length=8192, debug=False, uart=None):
        self.uart = uart if uart is not None else UART(
            uart_bus, baudrate=baudrate, rxbuf=rxbuf
        )
        self.rx_buffer = bytearray()
        self.max_line_length = max_line_length
        self.discarding_oversize_line = False
        self.waiting_for_json_start = True
        self.debug = debug
        self._log("[UART] Initialized: UART%d, baud=%d, 8N1, rxbuf=%d" %
                  (uart_bus, baudrate, rxbuf))
        self._log("[UART] Expected wiring: OpenMV TX -> C3 RX; OpenMV RX <- C3 TX; GND common")

    def _log(self, message):
        if self.debug:
            print(message)

    def poll(self, command_handler):
        """Drain available UART data and dispatch each complete JSON object.

        command_handler receives one decoded dict. Any exception is converted
        to the same generic error response/event used by the V1 firmware.
        """
        try:
            while True:
                available = self.uart.any()
                if not available:
                    break
                chunk = self.uart.read(available)
                if not chunk:
                    break
                for value in chunk:
                    # Resynchronize at the next JSON object start after noise.
                    if self.waiting_for_json_start:
                        if value != 123:  # '{'
                            if value == 10:
                                continue
                            continue
                        self.waiting_for_json_start = False
                        self.rx_buffer = bytearray([value])
                        continue

                    if value == 10:  # LF
                        if self.discarding_oversize_line:
                            self._log("[UART ERROR] Oversized RX line discarded")
                            self.discarding_oversize_line = False
                            self.rx_buffer = bytearray()
                            self.waiting_for_json_start = True
                            continue
                        raw_line = bytes(self.rx_buffer)
                        self.rx_buffer = bytearray()
                        self.waiting_for_json_start = True
                        if raw_line.endswith(b"\r"):
                            raw_line = raw_line[:-1]
                        if raw_line:
                            self._handle_line(raw_line, command_handler)
                    elif self.discarding_oversize_line:
                        continue
                    elif len(self.rx_buffer) >= self.max_line_length:
                        self.discarding_oversize_line = True
                        self.rx_buffer = bytearray()
                    else:
                        self.rx_buffer.append(value)
        except Exception as e:
            self._log("[UART ERROR] Read/dispatch failed: %s" % e)

    def _handle_line(self, raw_line, command_handler):
        try:
            line = raw_line.decode("utf-8").strip()
            command = ujson.loads(line)
            if not isinstance(command, dict):
                raise ValueError("JSON root must be an object")
            self._log("[UART RX LINE] bytes=%d command=%r" %
                      (len(raw_line), command.get("cmd")))
            command_handler(command)
        except Exception as e:
            self._log("[UART PARSE ERROR] bytes=%d prefix=%r error=%s" %
                      (len(raw_line), raw_line[:160], e))
            self.send_json({
                "type": "resp",
                "status": "error",
                "reason": "invalid_json_or_command"
            })
            self.send_json({
                "type": "event",
                "event": "error",
                "severity": "error",
                "code": "UART_COMMAND_PROCESSING_FAILED"
            })

    def send_json(self, data):
        """Serialize one response/event and terminate it with LF."""
        try:
            payload = ujson.dumps(data) + "\n"
            self.uart.write(payload)
            self._log("[UART TX] %s" % payload.strip())
        except Exception as e:
            self._log("[UART TX ERROR] %s" % e)
