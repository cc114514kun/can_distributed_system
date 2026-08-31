"""
host_tool.py — Industrial CAN Gateway Host Software (PC 上位机).

Tkinter-based host tool implementing the 6 modules from 上位机实现要求.docx:
  Communication / Monitoring / Configuration / Fault / Logging / Maintenance.
Talks to the F407 gateway over UART using the text-line maintenance protocol
(see protocol_spec.md). Requires: `pip install pyserial`.
"""
from __future__ import annotations

import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

import serial_comm
import protocol as P
from data_model import DataModel
from csv_logger import CsvLogger
import panels

POLL_INTERVAL = 2.0      # seconds between monitoring polls
REFRESH_MS = 500         # UI refresh period


class HostApp:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("CAN 网关上位机  v1.0  (F407 Gateway Host)")
        self.root.geometry("760x620")

        self.comm = serial_comm.SerialComm()
        self.model = DataModel()
        self.logger = CsvLogger()
        self.running = True

        self._build_ui()
        self._build_panels()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(REFRESH_MS, self._refresh_ui)
        self.poll_thread = threading.Thread(target=self._poller, daemon=True)
        self.poll_thread.start()

    # ---------------- UI ----------------
    def _build_ui(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=4, pady=4)

    def _build_panels(self):
        self.panels = {}
        specs = [
            ("通信", panels.CommPanel),
            ("监控", panels.MonitorPanel),
            ("配置", panels.ConfigPanel),
            ("故障", panels.FaultPanel),
            ("日志", panels.LoggingPanel),
            ("性能", panels.PerfPanel),
            ("维护", panels.MaintenancePanel),
        ]
        for title, cls in specs:
            p = cls(self)
            self.panels[title] = p
            self.notebook.add(p.frame, text=title)

    # ---------------- push stream (device-initiated) ----------------
    def _on_push(self, line):
        """Callback for 'PUSH ...' lines from the gateway's active-push task."""
        try:
            parsed = P.parse_push_line(line)
        except Exception:
            parsed = None
        if parsed is not None:
            self.model.apply_push_line(parsed)

    # ---------------- connection control (called by CommPanel) ----------------
    def connect(self, port, baud):
        self.comm.connect(port, baud)
        self.comm.set_push_callback(self._on_push)
        self.model.mark_connected(True)

    def disconnect(self):
        try:
            self.comm.disconnect()
        finally:
            self.model.mark_connected(False)

    # ---------------- poller (background thread) ----------------
    def _poller(self):
        while self.running:
            if self.comm.is_open:
                try:
                    for cmd, parser, updater in (
                        (P.CMD_GET_STATUS, P.parse_status, self.model.update_status),
                        (P.CMD_GET_NODE, P.parse_node_list, self.model.update_nodes),
                        (P.CMD_GET_SENSOR, P.parse_sensor, self.model.update_sensors),
                        (P.CMD_GET_FAULT, P.parse_fault, self.model.update_faults),
                        (P.CMD_GET_PERF, P.parse_perf, self.model.update_perf),
                        (P.CMD_GET_PUSH, P.parse_push_cfg, self.model.update_push_cfg),
                    ):
                        lines = self.comm.query(cmd)
                        parsed = parser(lines)
                        if parsed is None:
                            print(f"[comm warn] {cmd} failed: {lines!r}")
                            self.model.record_comm_error()
                        else:
                            updater(parsed)
                    # record samples to CSV if logging active
                    if self.logger.recording:
                        snap = self.model.snapshot()
                        for nid, s in snap["sensors"].items():
                            online = snap["nodes"].get(nid, {}).get("online", False)
                            self.logger.log_sample(nid, s, online)
                except Exception as e:
                    print(f"[comm error] poll exception: {e!r}")
                    self.model.record_comm_error()
            time.sleep(POLL_INTERVAL)

    # ---------------- UI refresh ----------------
    def _refresh_ui(self):
        if not self.running:
            return
        for p in self.panels.values():
            try:
                p.refresh()
            except Exception:
                pass
        self.root.after(REFRESH_MS, self._refresh_ui)

    def _on_close(self):
        self.running = False
        try:
            self.logger.stop()
        except Exception:
            pass
        try:
            self.comm.disconnect()
        except Exception:
            pass
        self.root.destroy()


def main():
    try:
        import serial  # noqa: F401  (fail fast if pyserial missing)
    except ImportError:
        print("ERROR: pyserial not installed. Run: pip install pyserial")
        return
    app = HostApp()
    app.root.mainloop()


if __name__ == "__main__":
    main()
