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

        # Lightweight counters make failures observable without changing the
        # application's polling cadence or wire protocol.
        self.stats = {
            "rx_bytes": 0,
            "rx_lines": 0,
            "rx_read_errors": 0,
            "rx_parse_errors": 0,
            "rx_handler_errors": 0,
            "rx_oversize_lines": 0,
            "tx_messages": 0,
            "tx_errors": 0
        }
        self.last_error = None
        self._log("[UART] Initialized: UART%d, baud=%d, 8N1, rxbuf=%d" %
                  (uart_bus, baudrate, rxbuf))
        self._log("[UART] Expected wiring: OpenMV TX -> C3 RX; OpenMV RX <- C3 TX; GND common")

    def _log(self, message):
        if self.debug:
            print(message)

    def _reset_rx_line(self):
        self.rx_buffer = bytearray()
        self.discarding_oversize_line = False
        self.waiting_for_json_start = True

    def poll(self, command_handler):
        """Drain currently available UART data and dispatch complete JSONL commands."""
        while True:
            try:
                available = self.uart.any()
                if not available:
                    break
                chunk = self.uart.read(available)
                if not chunk:
                    break
            except Exception as exc:
                self.stats["rx_read_errors"] += 1
                self.last_error = "UART read failed: %s" % exc
                self._log("[UART RX ERROR] %s" % self.last_error)
                break

            self.stats["rx_bytes"] += len(chunk)
            for value in chunk:
                try:
                    # Resynchronize at the next JSON object start after noise.
                    if self.waiting_for_json_start:
                        if value != 123:  # '{'
                            continue
                        self.waiting_for_json_start = False
                        self.rx_buffer = bytearray([value])
                        continue

                    if value == 10:  # LF
                        if self.discarding_oversize_line:
                            self.stats["rx_oversize_lines"] += 1
                            self._log("[UART ERROR] Oversized RX line discarded")
                            self._reset_rx_line()
                            continue

                        raw_line = bytes(self.rx_buffer)
                        self._reset_rx_line()
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
                except Exception as exc:
                    # A malformed line or unexpected parser-state error must
                    # not silently discard the rest of the current UART chunk.
                    self.stats["rx_read_errors"] += 1
                    self.last_error = "UART receive state error: %s" % exc
                    self._log("[UART RX ERROR] %s" % self.last_error)
                    self._reset_rx_line()

    def _handle_line(self, raw_line, command_handler):
        try:
            line = raw_line.decode("utf-8").strip()
            command = ujson.loads(line)
            if not isinstance(command, dict):
                raise ValueError("JSON root must be an object")
        except Exception as exc:
            self.stats["rx_parse_errors"] += 1
            self.last_error = "JSON parse failed: %s" % exc
            self._log("[UART PARSE ERROR] bytes=%d prefix=%r error=%s" %
                      (len(raw_line), raw_line[:160], exc))
            self.send_json({
                "type": "resp",
                "status": "error",
                "reason": "invalid_json"
            })
            return

        self.stats["rx_lines"] += 1
        self._log("[UART RX LINE] bytes=%d command=%r" %
                  (len(raw_line), command.get("cmd")))
        try:
            command_handler(command)
        except Exception as exc:
            self.stats["rx_handler_errors"] += 1
            self.last_error = "Command handler failed: %s" % exc
            self._log("[UART COMMAND ERROR] cmd=%r error=%s" %
                      (command.get("cmd"), exc))
            self.send_json({
                "type": "resp",
                "status": "error",
                "reason": "command_processing_failed",
                "detail": str(exc)
            })
            self.send_json({
                "type": "event",
                "event": "error",
                "severity": "error",
                "code": "UART_COMMAND_PROCESSING_FAILED"
            })

    def send_json(self, data):
        """Serialize and transmit one JSONL response/event; return success."""
        try:
            payload = ujson.dumps(data) + "\n"
            written = self.uart.write(payload)
            if isinstance(written, int) and written != len(payload):
                self.stats["tx_errors"] += 1
                self.last_error = "UART short write: %d/%d bytes" % (
                    written, len(payload)
                )
                self._log("[UART TX ERROR] %s" % self.last_error)
                return False
            self.stats["tx_messages"] += 1
            self._log("[UART TX] %s" % payload.strip())
            return True
        except Exception as exc:
            self.stats["tx_errors"] += 1
            self.last_error = "UART transmit failed: %s" % exc
            self._log("[UART TX ERROR] %s" % self.last_error)
            return False
