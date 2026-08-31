"""
csv_logger.py — data acquisition & logging (doc module 5).

Host-side CSV recording of sensor samples. No firmware command needed; the host
samples whatever GET_SENSOR returns while recording is active.
"""
from __future__ import annotations

import csv
import os
import time
import threading


class CsvLogger:
    def __init__(self):
        self.lock = threading.Lock()
        self.f = None
        self.writer = None
        self.path = None
        self.recording = False

    def start(self, path: str):
        with self.lock:
            if self.recording:
                return False
            self.path = path
            # append timestamp header row
            self.f = open(path, "w", newline="", encoding="utf-8-sig")
            self.writer = csv.writer(self.f)
            self.writer.writerow([
                "timestamp", "node_id", "volt_q100", "voltage_v",
                "temp_q100", "temp_c", "key_state", "online"
            ])
            self.recording = True
            return True

    def stop(self):
        with self.lock:
            if not self.recording:
                return
            try:
                self.f.flush()
                self.f.close()
            finally:
                self.f = None
                self.writer = None
                self.recording = False

    def log_sample(self, node_id, sensor: dict, online: bool):
        """sensor: dict(id, volt_q100, temp, key). Write one CSV row."""
        with self.lock:
            if not self.recording or self.writer is None:
                return
            ts = time.strftime("%Y-%m-%d %H:%M:%S")
            volt = sensor.get("volt_q100", 0) / 100.0
            temp = sensor.get("temp", 0) / 100.0
            self.writer.writerow([
                ts, node_id,
                sensor.get("volt_q100", 0), f"{volt:.2f}",
                sensor.get("temp", 0), f"{temp:.2f}",
                sensor.get("key", 0), int(online)
            ])

    @staticmethod
    def default_path():
        os.makedirs("logs", exist_ok=True)
        return os.path.join("logs", time.strftime("sensor_%Y%m%d_%H%M%S.csv"))
