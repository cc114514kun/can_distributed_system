"""
data_model.py — shared application state for the host tool.

Holds the latest snapshot of gateway/node/sensor/config/fault/statistics,
plus a rolling history buffer for trend charts. Thread-safe via a lock so the
polling thread can update while the UI thread reads.
"""
from __future__ import annotations

import threading
import time

MAX_NODE_NUM = 4
HISTORY_LEN = 600  # samples kept per node for trend charts


class DataModel:
    def __init__(self):
        self.lock = threading.RLock()
        self.connected = False
        self.last_update = 0.0
        self.comm_errors = 0
        self.rx_total = 0

        # comm stats (GET_STATUS)
        self.invalid_frames = 0
        self.busoff_retry = 0

        # nodes / sensors
        self.nodes = {}      # id -> dict(id, seq, online)
        self.sensors = {}    # id -> dict(id, volt_q100, temp, key, ts)

        # config (GET_CONFIG)
        self.config = None   # dict or None

        # faults (GET_FAULT)
        self.faults = []     # list of dict(ts, code, node, param)

        # versions / sys (strings)
        self.version_lines = []
        self.sys_lines = []

        # FreeRTOS performance snapshot (GET_PERF)
        self.perf = None   # dict or None

        # P2 active-push state
        self.push_enabled = False
        self.push_period_ms = 0
        self.push_last_ts = 0.0
        self.push_status = None   # dict or None (last PUSH STATUS)
        self.push_nodes = {}      # id -> dict (raw PUSH NODE lines)

        # history for trends: id -> list of (t, temp, volt)
        self.history = {}

    # ---- update API (called by poller) ----
    def mark_connected(self, ok: bool):
        with self.lock:
            self.connected = ok

    def record_comm_error(self):
        with self.lock:
            self.comm_errors += 1

    def update_status(self, d):
        if d is None:
            self.record_comm_error()
            return
        with self.lock:
            self.invalid_frames = d.get("invalid_frames", 0)
            self.busoff_retry = d.get("busoff_retry", 0)
            self.last_update = time.time()
            self.rx_total += 1

    def update_nodes(self, lst):
        if lst is None:
            self.record_comm_error()
            return
        with self.lock:
            self.nodes = {n["id"]: n for n in lst}
            self.last_update = time.time()

    def update_sensors(self, lst):
        if lst is None:
            self.record_comm_error()
            return
        with self.lock:
            now = time.time()
            for s in lst:
                self.sensors[s["id"]] = s
                hid = s["id"]
                buf = self.history.setdefault(hid, [])
                buf.append((now, s["temp"] / 100.0, s["volt_q100"] / 100.0))
                if len(buf) > HISTORY_LEN:
                    buf.pop(0)
            self.last_update = time.time()

    def update_faults(self, d):
        if d is None:
            self.record_comm_error()
            return
        with self.lock:
            self.faults = d.get("events", [])
            self.last_update = time.time()

    def update_config(self, d):
        with self.lock:
            self.config = d

    def update_version(self, d):
        with self.lock:
            if d:
                self.version_lines = d.get("lines", [])

    def update_sys(self, lines):
        with self.lock:
            self.sys_lines = lines

    def update_perf(self, d):
        if d is None:
            self.record_comm_error()
            return
        with self.lock:
            self.perf = d
            self.last_update = time.time()

    def update_push_cfg(self, d):
        with self.lock:
            if d is None:
                return
            self.push_enabled = d.get("enable", False)
            self.push_period_ms = d.get("period_ms", 0)

    def apply_push_line(self, parsed):
        """Consume one device-initiated PUSH line (parsed dict from protocol)."""
        if not parsed:
            return
        with self.lock:
            self.push_last_ts = time.time()
            self.rx_total += 1
            if parsed.get("type") == "status":
                self.push_status = parsed
            elif parsed.get("type") == "node":
                nid = parsed.get("n")
                if nid is None:
                    return
                self.push_nodes[nid] = parsed
                # Mirror into sensor/node tables so the Monitor panel reflects
                # the real-time push stream directly.
                self.sensors[nid] = {
                    "id": nid,
                    "volt_q100": parsed.get("volt", 0),
                    "temp": parsed.get("temp", 0),
                    "k1": parsed.get("k1", False),
                    "k2": parsed.get("k2", False),
                }
                self.nodes[nid] = {
                    "id": nid,
                    "seq": parsed.get("seq", 0),
                    "online": parsed.get("on", False),
                }
                self.last_update = time.time()

    # ---- read API (called by UI) ----
    def snapshot(self):
        with self.lock:
            return {
                "connected": self.connected,
                "last_update": self.last_update,
                "comm_errors": self.comm_errors,
                "rx_total": self.rx_total,
                "invalid_frames": self.invalid_frames,
                "busoff_retry": self.busoff_retry,
                "nodes": dict(self.nodes),
                "sensors": dict(self.sensors),
                "config": self.config,
                "faults": list(self.faults),
                "version_lines": list(self.version_lines),
                "sys_lines": list(self.sys_lines),
                "perf": self.perf,
                "push_enabled": self.push_enabled,
                "push_period_ms": self.push_period_ms,
                "push_last_ts": self.push_last_ts,
                "push_status": self.push_status,
                "push_nodes": dict(self.push_nodes),
                "history": {k: list(v) for k, v in self.history.items()},
            }
