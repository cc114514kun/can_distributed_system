"""
protocol.py — F407 <-> PC UART maintenance protocol helpers.

Defines command builders, CRC8 (poly 0x07, init 0, same as CAN sensor frame),
and response parsers. Pure-python, no hardware dependency.

Parsers tolerate unrelated debug/log lines interleaved on the same UART by
locating the first line that starts with "OK" or "ERR" and parsing from there.
"""
from __future__ import annotations

import struct
import re

# ---- command names (host -> device) ----
CMD_GET_STATUS = "GET_STATUS"
CMD_GET_NODE = "GET_NODE"
CMD_GET_SENSOR = "GET_SENSOR"
CMD_GET_FAULT = "GET_FAULT"
CMD_CLEAR_FAULT = "CLEAR_FAULT"
CMD_GET_VERSION = "GET_VERSION"
CMD_GET_SYS = "GET_SYS"
CMD_GET_CONFIG = "GET_CONFIG"
CMD_SET_CONFIG = "SET_CONFIG"
CMD_CLEAR_COUNTERS = "CLEAR_COUNTERS"
CMD_RESET_DEVICE = "RESET_DEVICE"
CMD_GET_PERF = "GET_PERF"
CMD_GET_PUSH = "GET_PUSH"
CMD_SET_PUSH = "SET_PUSH"


def crc8(data: bytes) -> int:
    """CRC8, polynomial 0x07, init 0x00 (mirrors firmware Cli_Crc8)."""
    crc = 0
    for b in data:
        crc ^= b & 0xFF
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def cmd_with_crc(cmd: str) -> str:
    """
    Append a 2-hex-digit CRC8 to any command line.
    The firmware recomputes CRC8 over the command bytes (everything before the
    trailing space) and rejects the frame if it mismatches -> ERR BAD_FRAME.
    A frame without a trailing CRC token is still accepted (backward compatible).
    """
    return f"{cmd} {crc8(cmd.encode('ascii')):02X}"


def build_set_config(timeout_ms: int, temp_hi_q100: int,
                     period_ms: int, node_mask: int, with_crc: bool = True) -> str:
    """
    Build a SET_CONFIG command line.
    Returns e.g. "SET_CONFIG 500 8000 1000 0000000F 3C"
    """
    node_mask &= 0xFFFFFFFF
    cmd = f"{CMD_SET_CONFIG} {int(timeout_ms)} {int(temp_hi_q100)} {int(period_ms)} {node_mask:08X}"
    if with_crc:
        payload = struct.pack("<iiII", int(timeout_ms), int(temp_hi_q100),
                              int(period_ms), node_mask)
        cmd += f" {crc8(payload):02X}"
    return cmd


# ---------------- response helpers ----------------

def _find_header(lines):
    """Return (index, header_line) of the first line that starts with OK/ERR.
    Returns (None, None) if no response header is found."""
    if not lines:
        return None, None
    for i, ln in enumerate(lines):
        s = ln.strip()
        if s.startswith("OK") or s.startswith("ERR"):
            return i, s
    return None, None


def _is_ok(lines):
    _, header = _find_header(lines)
    return bool(header) and header.startswith("OK")


def _is_err(lines):
    _, header = _find_header(lines)
    return bool(header) and header.startswith("ERR")


# ---------------- response parsers ----------------

def parse_status(lines):
    """GET_STATUS -> dict(invalid_frames, busoff_retry) or None on error."""
    if not _is_ok(lines):
        return None
    _, header = _find_header(lines)
    d = {}
    m = re.search(r"Invalid:(\d+)", header)
    if m:
        d["invalid_frames"] = int(m.group(1))
    m = re.search(r"BusOffRetry:(\d+)", header)
    if m:
        d["busoff_retry"] = int(m.group(1))
    return d


def parse_node_list(lines):
    """
    GET_NODE -> list of dict(id, seq, online) or None on error.
    Each data line: "  ID:1 Seq:12 Online:1"
    """
    idx, header = _find_header(lines)
    if idx is None or not header.startswith("OK"):
        return None
    nodes = []
    for ln in lines[idx + 1:]:
        m = re.match(r"\s*ID:(\d+)\s+Seq:(\d+)\s+Online:(\d+)", ln)
        if m:
            nodes.append({
                "id": int(m.group(1)),
                "seq": int(m.group(2)),
                "online": int(m.group(3)) != 0,
            })
    return nodes


def parse_sensor(lines):
    """
    GET_SENSOR -> list of dict(id, volt_q100, temp, k1, k2) or None.
    Each data line: "  N1 VoltQ100:1234 Temp:256 K1:1 K2:1"
    K1/K2 are the low 2 bits of the combined key_state byte from the CAN node.
    """
    idx, header = _find_header(lines)
    if idx is None or not header.startswith("OK"):
        return None
    out = []
    for ln in lines[idx + 1:]:
        m = re.match(r"\s*N(\d+)\s+VoltQ100:(\d+)\s+Temp:(-?\d+)\s+K1:(\d+)\s+K2:(\d+)", ln)
        if m:
            out.append({
                "id": int(m.group(1)),
                "volt_q100": int(m.group(2)),
                "temp": int(m.group(3)),
                "k1": int(m.group(4)) != 0,
                "k2": int(m.group(5)) != 0,
            })
    return out


def parse_fault(lines):
    """
    GET_FAULT -> dict(count, events[list of dict(ts, code, node, param)]) or None.
    Each data line: "  TS:123 CODE:FAULT_NODE_OFFLINE NODE:2 PARAM:0"
    """
    idx, header = _find_header(lines)
    if idx is None or not header.startswith("OK"):
        return None
    count = 0
    m = re.search(r"FAULT_CNT:(\d+)", header)
    if m:
        count = int(m.group(1))
    events = []
    for ln in lines[idx + 1:]:
        m = re.match(r"\s*TS:(\d+)\s+CODE:(\S+)\s+NODE:(-?\d+)\s+PARAM:(\d+)", ln)
        if m:
            events.append({
                "ts": int(m.group(1)),
                "code": m.group(2),
                "node": int(m.group(3)),
                "param": int(m.group(4)),
            })
    return {"count": count, "events": events}


def parse_config(lines):
    """
    GET_CONFIG -> dict(timeout_ms, temp_hi_q100, period_ms, node_mask) or None.
    "OK CONFIG TIMEOUT:500 TEMP_HI:8000 PERIOD:1000 NODEMASK:0x0000000F"
    """
    if not _is_ok(lines):
        return None
    _, header = _find_header(lines)
    d = {}
    m = re.search(r"TIMEOUT:(\d+)", header)
    if m:
        d["timeout_ms"] = int(m.group(1))
    m = re.search(r"TEMP_HI:(-?\d+)", header)
    if m:
        d["temp_hi_q100"] = int(m.group(1))
    m = re.search(r"PERIOD:(\d+)", header)
    if m:
        d["period_ms"] = int(m.group(1))
    m = re.search(r"NODEMASK:0x([0-9A-Fa-f]+)", header)
    if m:
        d["node_mask"] = int(m.group(1), 16)
    return d


def parse_version(lines):
    """GET_VERSION -> dict(raw list of strings) or None."""
    idx, header = _find_header(lines)
    if idx is None or not header.startswith("OK"):
        return None
    return {"lines": [ln.strip() for ln in lines[idx:]]}


def parse_ok_only(lines):
    """For CLEAR_FAULT / CLEAR_COUNTERS / CONFIG_SAVED — return True if OK."""
    return _is_ok(lines)


def parse_reset(lines):
    """RESET_DEVICE -> True if acknowledged (device will reboot)."""
    return _is_ok(lines)


def parse_perf(lines):
    """
    GET_PERF -> dict with task list + queue/rate/heap stats, or None on error.

    Example firmware output:
      OK PERF
        CanGw          P: 4 CPU: 3% HWM: 380B
        ...
      QPEAK canrx=8 fault=2 report=12
      RATE canrx=45/s cantx=120/s uarttx=512B/s
      HEAP_PEAK_USED=1234B TOTAL=98304B
    """
    idx, header = _find_header(lines)
    if idx is None or not header.startswith("OK"):
        return None
    tasks = []
    qpeak = {}
    rate = {}
    heap = {}
    for ln in lines[idx + 1:]:
        m = re.match(
            r"\s*(\S+)\s+P:(\d+)\s+CPU:(\d+)%\s+HWM:(\d+)B", ln)
        if m:
            tasks.append({
                "name": m.group(1),
                "prio": int(m.group(2)),
                "cpu_pct": int(m.group(3)),
                "hwm_bytes": int(m.group(4)),
            })
            continue
        m = re.match(r"\s*QPEAK\s+canrx=(\d+)\s+fault=(\d+)\s+report=(\d+)", ln)
        if m:
            qpeak = {"canrx": int(m.group(1)),
                     "fault": int(m.group(2)),
                     "report": int(m.group(3))}
            continue
        m = re.match(
            r"\s*RATE\s+canrx=(\d+)/s\s+cantx=(\d+)/s\s+uarttx=(\d+)B/s", ln)
        if m:
            rate = {"canrx": int(m.group(1)),
                    "cantx": int(m.group(2)),
                    "uarttx": int(m.group(3))}
            continue
        m = re.match(r"\s*HEAP_PEAK_USED=(\d+)B\s+TOTAL=(\d+)B", ln)
        if m:
            heap = {"peak_used": int(m.group(1)),
                    "total": int(m.group(2))}
    return {"tasks": tasks, "qpeak": qpeak, "rate": rate, "heap": heap}


def parse_push_cfg(lines):
    """
    GET_PUSH -> dict(enable:bool, period_ms:int) or None.
    "OK PUSH CFG ENABLE:1 PERIOD:1000"
    """
    if not _is_ok(lines):
        return None
    _, header = _find_header(lines)
    d = {}
    m = re.search(r"ENABLE:(\d+)", header)
    if m:
        d["enable"] = (int(m.group(1)) != 0)
    m = re.search(r"PERIOD:(\d+)", header)
    if m:
        d["period_ms"] = int(m.group(1))
    return d


def parse_push_line(line):
    """
    Parse a single device-initiated PUSH line into a dict.

    "PUSH STATUS can_invalid=0 busoff_retry=0 uptime=123s"
      -> {"type":"status", "can_invalid":0, "busoff_retry":0, "uptime":123}
    "PUSH NODE n=1 on=1 volt=243 temp=2464 k1=1 k2=0 seq=12"
      -> {"type":"node", "n":1, "on":1, "volt":243, "temp":2464,
          "k1":1, "k2":1, "seq":12}

    Returns None if the line is not a recognized PUSH line.
    """
    if not line or not line.startswith("PUSH "):
        return None
    body = line[len("PUSH "):].strip()
    if body.startswith("STATUS"):
        d = {"type": "status"}
        m = re.search(r"can_invalid=(\d+)", body)
        if m:
            d["can_invalid"] = int(m.group(1))
        m = re.search(r"busoff_retry=(\d+)", body)
        if m:
            d["busoff_retry"] = int(m.group(1))
        m = re.search(r"uptime=(\d+)s", body)
        if m:
            d["uptime"] = int(m.group(1))
        return d
    if body.startswith("NODE"):
        d = {"type": "node"}
        m = re.search(r"n=(\d+)", body)
        if m:
            d["n"] = int(m.group(1))
        m = re.search(r"on=(\d+)", body)
        if m:
            d["on"] = (int(m.group(1)) != 0)
        m = re.search(r"volt=(\d+)", body)
        if m:
            d["volt"] = int(m.group(1))
        m = re.search(r"temp=(-?\d+)", body)
        if m:
            d["temp"] = int(m.group(1))
        m = re.search(r"k1=(\d+)", body)
        if m:
            d["k1"] = (int(m.group(1)) != 0)
        m = re.search(r"k2=(\d+)", body)
        if m:
            d["k2"] = (int(m.group(1)) != 0)
        m = re.search(r"seq=(\d+)", body)
        if m:
            d["seq"] = int(m.group(1))
        return d
    return None
