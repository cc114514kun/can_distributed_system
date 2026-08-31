"""
serial_comm.py — UART transport for the host tool.

Full-duplex design:
  * A background reader thread continuously drains the serial port and splits
    incoming lines into two streams:
      - lines prefixed "PUSH "  -> delivered to the registered push callback
        (device-initiated real-time data; see P2 active-push feature)
      - all other lines         -> placed on an internal response queue that
        query() consumes to match command responses
  * query() sends a command (CRC8 appended) and collects its response from the
    queue, retrying on transient truncation. It no longer resets the input
    buffer, so spontaneous PUSH lines are never discarded.
"""
from __future__ import annotations

import time
import threading
import queue
import serial
import serial.tools.list_ports

from protocol import cmd_with_crc

DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 1.0  # seconds to collect a multi-line response
MAX_RETRY = 5          # resend on transient truncation (BAD_FRAME / Unknown cmd / timeout)


# Errors that indicate a corrupted/truncated frame rather than a logical failure.
_RETRYABLE_PREFIXES = ("ERR Unknown cmd", "ERR BAD_FRAME", "ERR TIMEOUT")


def _is_retryable(lines: list) -> bool:
    if not lines:
        return True  # timeout / no response
    head = lines[0].strip()
    for p in _RETRYABLE_PREFIXES:
        if p and head.startswith(p):
            return True
    return False


class SerialComm:
    def __init__(self, baud: int = DEFAULT_BAUD, timeout: float = DEFAULT_TIMEOUT):
        self.baud = baud
        self.timeout = timeout
        self.ser = None
        self.port = None
        self._partial = b""
        self._lock = threading.RLock()
        self._resp_q = queue.Queue()      # command responses (non-PUSH lines)
        self._push_cb = None              # callback for "PUSH ..." lines
        self._reader = None
        self._reader_alive = False

    # ---- port management ----
    @staticmethod
    def list_ports():
        """Return list of (device, description) for UI combo box."""
        ports = []
        for p in serial.tools.list_ports.comports():
            ports.append((p.device, p.description or p.device))
        return ports

    @property
    def is_open(self):
        return self.ser is not None and self.ser.is_open

    def connect(self, port: str, baud: int = None) -> bool:
        if baud:
            self.baud = baud
        try:
            self.ser = serial.Serial(port, self.baud, timeout=0.1,
                                     write_timeout=1.0)
            self.port = port
            self.ser.reset_input_buffer()
            self._partial = b""
            try:
                while not self._resp_q.empty():
                    self._resp_q.get_nowait()
            except Exception:
                pass
            self._reader_alive = True
            self._reader = threading.Thread(target=self._reader_run, daemon=True)
            self._reader.start()
            return True
        except (serial.SerialException, OSError) as e:
            self.ser = None
            self.port = None
            raise ConnectionError(f"Open {port} failed: {e}")

    def disconnect(self):
        self._reader_alive = False
        if self._reader is not None:
            self._reader.join(timeout=1.0)
            self._reader = None
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
            self.port = None

    def set_push_callback(self, cb):
        """Register callback invoked for each device-initiated 'PUSH ...' line."""
        self._push_cb = cb

    # ---- low level line IO ----
    def send_line(self, text: str):
        """Send a single command line (CRLF terminated)."""
        if not self.is_open:
            raise ConnectionError("Port not open")
        with self._lock:
            data = (text + "\r\n").encode("utf-8", errors="replace")
            self.ser.write(data)
            self.ser.flush()

    def _read_available_lines(self):
        """Read whatever is available now, split into complete CRLF lines."""
        lines = []
        if self.ser.in_waiting or self._partial:
            raw = self.ser.read(self.ser.in_waiting or 1)
            if raw:
                self._partial += raw
        while b"\n" in self._partial:
            line, self._partial = self._partial.split(b"\n", 1)
            if line.endswith(b"\r"):
                line = line[:-1]
            try:
                lines.append(line.decode("utf-8", errors="replace").rstrip("\r"))
            except Exception:
                lines.append(line.decode("latin-1", errors="replace"))
        return lines

    def _reader_run(self):
        """Background: read lines, route PUSH to callback, others to resp queue."""
        while self._reader_alive:
            try:
                with self._lock:
                    if self.ser is not None and self.ser.is_open:
                        lines = self._read_available_lines()
                    else:
                        lines = []
            except Exception:
                lines = []
            for ln in lines:
                if ln.startswith("PUSH "):
                    if self._push_cb:
                        try:
                            self._push_cb(ln)
                        except Exception:
                            pass
                else:
                    self._resp_q.put(ln)
            time.sleep(0.004)

    def _collect_response(self, timeout: float = None) -> list:
        """Collect response lines from the queue until idle or deadline."""
        timeout = timeout if timeout is not None else self.timeout
        deadline = time.time() + timeout
        idle = time.time() + 0.15  # gap with no data => response done
        collected = []
        while True:
            try:
                ln = self._resp_q.get(timeout=0.05)
            except queue.Empty:
                ln = None
            if ln is not None:
                collected.append(ln)
                idle = time.time() + 0.15
            if time.time() > idle:
                break
            if time.time() > deadline:
                break
        return collected

    def query(self, cmd: str, timeout: float = None) -> list:
        """
        Send a command (with CRC8 appended) and collect its response lines.
        Retries up to MAX_RETRY times on transient frame corruption
        (truncation -> ERR Unknown cmd / ERR BAD_FRAME) or no response.
        Returns the last response; callers should treat retries-exhausted as a
        real comm error only when the final response is still retryable/empty.
        """
        framed = cmd_with_crc(cmd)
        last = []
        for attempt in range(MAX_RETRY):
            # drop any response left from a previous attempt (push lines are
            # routed to the callback, never sit in this queue)
            try:
                while not self._resp_q.empty():
                    self._resp_q.get_nowait()
            except Exception:
                pass
            self.send_line(framed)
            last = self._collect_response(timeout)
            if not _is_retryable(last):
                return last
            time.sleep(0.05 * (attempt + 1))
        return last

    def read_lines(self, timeout: float = None) -> list:
        """Best-effort collect whatever is currently queued (legacy/debug)."""
        if not self.is_open:
            return []
        return self._collect_response(timeout)
