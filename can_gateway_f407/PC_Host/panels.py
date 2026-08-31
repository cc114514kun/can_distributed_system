"""
panels.py — the six function-module tabs of the host tool UI.

Module mapping (per 上位机实现要求.docx):
  1. System Communication   -> CommPanel
  2. Device Monitoring       -> MonitorPanel
  3. Device Configuration    -> ConfigPanel
  4. Fault Diagnosis         -> FaultPanel
  5. Data Acquisition/Log    -> LoggingPanel
  6. System Maintenance      -> MaintenancePanel
"""
from __future__ import annotations

import time
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import protocol as P
from csv_logger import CsvLogger
from charts import LineChart

BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]


# ---------------------------------------------------------------- 1. Comm
class CommPanel:
    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook)
        self._build()

    def _build(self):
        f = self.frame
        top = ttk.LabelFrame(f, text="串口连接 / Serial Connection", padding=8)
        top.pack(fill="x", padx=8, pady=6)

        ttk.Label(top, text="端口:").grid(row=0, column=0, sticky="w")
        self.port_var = tk.StringVar()
        ports = P_serial_ports(self.app)
        self.port_cb = ttk.Combobox(top, textvariable=self.port_var, width=18,
                                    values=[p[0] for p in ports], state="readonly")
        if ports:
            self.port_var.set(ports[0][0])
        self.port_cb.grid(row=0, column=1, padx=4)
        ttk.Button(top, text="刷新", command=self._refresh_ports).grid(row=0, column=2, padx=2)

        ttk.Label(top, text="波特率:").grid(row=0, column=3, sticky="w", padx=(10, 0))
        self.baud_var = tk.StringVar(value="115200")
        ttk.Combobox(top, textvariable=self.baud_var, width=10,
                     values=[str(b) for b in BAUD_RATES], state="readonly"
                     ).grid(row=0, column=4, padx=4)

        self.connect_btn = ttk.Button(top, text="连接", command=self._toggle)
        self.connect_btn.grid(row=0, column=5, padx=6)

        self.status_var = tk.StringVar(value="未连接 / Disconnected")
        self.status_label = ttk.Label(top, textvariable=self.status_var, foreground="red")
        self.status_label.grid(row=1, column=0, columnspan=6, sticky="w", pady=(6, 0))

        # raw console
        mid = ttk.LabelFrame(f, text="调试控制台 / Raw Console (发送任意命令)", padding=8)
        mid.pack(fill="both", expand=True, padx=8, pady=6)

        self.console = scrolledtext.ScrolledText(mid, height=12, state="disabled")
        self.console.pack(fill="both", expand=True)

        bottom = ttk.Frame(mid)
        bottom.pack(fill="x", pady=(6, 0))
        self.cmd_var = tk.StringVar(value="GET_STATUS")
        ttk.Entry(bottom, textvariable=self.cmd_var).pack(side="left", fill="x",
                                                           expand=True)
        ttk.Button(bottom, text="发送", command=self._send_raw).pack(side="left", padx=6)

    def _refresh_ports(self):
        from serial_comm import SerialComm
        ports = SerialComm.list_ports()
        self.port_cb["values"] = [p[0] for p in ports]
        if ports and not self.port_var.get():
            self.port_var.set(ports[0][0])

    def _toggle(self):
        if self.app.comm.is_open:
            self.app.disconnect()
            self.connect_btn.config(text="连接")
            self.status_var.set("未连接 / Disconnected")
            self.status_label.configure(foreground="red")
        else:
            port = self.port_var.get()
            baud = int(self.baud_var.get())
            try:
                self.app.connect(port, baud)
                self.connect_btn.config(text="断开")
                self.status_var.set(f"已连接 {port} @ {baud}")
                self.status_label.configure(foreground="green")
                self._log(f"-- connected {port} @ {baud} --")
            except Exception as e:
                messagebox.showerror("连接失败", str(e))
                self._log(f"ERROR: {e}")

    def _send_raw(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        cmd = self.cmd_var.get().strip()
        if not cmd:
            return
        self._log(f">> {cmd}")
        try:
            lines = self.app.comm.query(cmd)
            if not lines:
                self._log("(无响应 / no response)")
            for ln in lines:
                self._log(ln)
        except Exception as e:
            self._log(f"ERROR: {e}")

    def _log(self, text):
        self.console.configure(state="normal")
        self.console.insert("end", text + "\n")
        self.console.see("end")
        self.console.configure(state="disabled")

    def refresh(self):
        pass


# ---------------------------------------------------------------- 2. Monitor
class MonitorPanel:
    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook)
        self._build()

    def _build(self):
        f = self.frame
        g = ttk.LabelFrame(f, text="网关状态 / Gateway Status", padding=8)
        g.pack(fill="x", padx=8, pady=6)
        self.gw_var = tk.StringVar(value="—")
        ttk.Label(g, textvariable=self.gw_var, justify="left").pack(anchor="w")

        nf = ttk.LabelFrame(f, text="节点状态 / Node Status", padding=8)
        nf.pack(fill="both", expand=True, padx=8, pady=6)
        cols = ("id", "online", "seq")
        self.node_tree = ttk.Treeview(nf, columns=cols, show="headings", height=5)
        for c, t in zip(cols, ("节点ID", "在线", "序列号")):
            self.node_tree.heading(c, text=t)
            self.node_tree.column(c, width=90, anchor="center")
        self.node_tree.pack(fill="both", expand=True)

        sf = ttk.LabelFrame(f, text="传感器实时数据 / Sensor Data", padding=8)
        sf.pack(fill="both", expand=True, padx=8, pady=6)
        cols = ("id", "volt", "temp", "k1", "k2")
        self.sensor_tree = ttk.Treeview(sf, columns=cols, show="headings", height=5)
        for c, t in zip(cols, ("节点ID", "电压(V)", "温度(℃)", "按键1", "按键2")):
            self.sensor_tree.heading(c, text=t)
            self.sensor_tree.column(c, width=80, anchor="center")
        self.sensor_tree.pack(fill="both", expand=True)

    def refresh(self):
        s = self.app.model.snapshot()
        conn = "已连接" if s["connected"] else "未连接"
        upd = time.strftime("%H:%M:%S", time.localtime(s["last_update"])) if s["last_update"] else "—"
        self.gw_var.set(
            f"连接: {conn}   最后更新: {upd}\n"
            f"CAN 非法帧: {s['invalid_frames']}   BusOff 重试: {s['busoff_retry']}\n"
            f"通信错误: {s['comm_errors']}   接收帧计数: {s['rx_total']}"
        )
        # nodes
        self._fill_tree(self.node_tree, ["id", "online", "seq"],
                        [{"id": k, "online": "是" if v["online"] else "否",
                          "seq": v["seq"]} for k, v in sorted(s["nodes"].items())])
        # sensors
        rows = []
        for k, v in sorted(s["sensors"].items()):
            rows.append({"id": k,
                         "volt": f"{v['volt_q100']/100.0:.2f}",
                         "temp": f"{v['temp']/100.0:.2f}",
                         "k1": "按下" if v.get("k1") else "松开",
                         "k2": "按下" if v.get("k2") else "松开"})
        self._fill_tree(self.sensor_tree, ["id", "volt", "temp", "k1", "k2"], rows)

    @staticmethod
    def _fill_tree(tree, cols, rows):
        tree.delete(*tree.get_children())
        for r in rows:
            tree.insert("", "end", values=[r[c] for c in cols])


# ---------------------------------------------------------------- 3. Config
class ConfigPanel:
    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook)
        self._build()

    def _build(self):
        f = self.frame
        g = ttk.LabelFrame(f, text="设备参数配置 / Configuration (GET/SET_CONFIG)",
                           padding=10)
        g.pack(fill="x", padx=8, pady=6)

        ttk.Label(g, text="节点离线超时(ms):").grid(row=0, column=0, sticky="w", pady=3)
        self.timeout_var = tk.StringVar()
        ttk.Entry(g, textvariable=self.timeout_var, width=14).grid(row=0, column=1, padx=6)

        ttk.Label(g, text="温度告警上限(Q100):").grid(row=1, column=0, sticky="w", pady=3)
        self.temp_var = tk.StringVar()
        ttk.Entry(g, textvariable=self.temp_var, width=14).grid(row=1, column=1, padx=6)

        ttk.Label(g, text="采样周期(ms):").grid(row=2, column=0, sticky="w", pady=3)
        self.period_var = tk.StringVar()
        ttk.Entry(g, textvariable=self.period_var, width=14).grid(row=2, column=1, padx=6)

        ttk.Label(g, text="节点使能掩码(hex):").grid(row=3, column=0, sticky="w", pady=3)
        self.mask_var = tk.StringVar()
        ttk.Entry(g, textvariable=self.mask_var, width=14).grid(row=3, column=1, padx=6)

        btns = ttk.Frame(g)
        btns.grid(row=4, column=0, columnspan=2, pady=8, sticky="w")
        ttk.Button(btns, text="读取", command=self._read).pack(side="left", padx=4)
        ttk.Button(btns, text="写入", command=self._write).pack(side="left", padx=4)
        ttk.Button(btns, text="恢复默认", command=self._default).pack(side="left", padx=4)

        self.status_var = tk.StringVar(value="")
        ttk.Label(g, textvariable=self.status_var, foreground="blue"
                  ).grid(row=5, column=0, columnspan=2, sticky="w")

    def _read(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        lines = self.app.comm.query(P.CMD_GET_CONFIG)
        d = P.parse_config(lines)
        if d:
            self.timeout_var.set(str(d["timeout_ms"]))
            self.temp_var.set(str(d["temp_hi_q100"]))
            self.period_var.set(str(d["period_ms"]))
            self.mask_var.set(f"0x{d['node_mask']:08X}")
            self.status_var.set("已读取当前配置")
        else:
            self.status_var.set("读取失败: " + (lines[0] if lines else "无响应"))

    def _write(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        try:
            timeout_ms = int(self.timeout_var.get())
            temp_hi = int(self.temp_var.get())
            period_ms = int(self.period_var.get())
            mask = int(self.mask_var.get(), 16)
        except ValueError:
            messagebox.showerror("格式错误", "请填写合法数字（掩码用 0x 十六进制）")
            return
        cmd = P.build_set_config(timeout_ms, temp_hi, period_ms, mask, with_crc=True)
        lines = self.app.comm.query(cmd)
        if lines and lines[0].startswith("OK"):
            self.status_var.set("写入成功并已保存到 Flash: " + lines[0])
        else:
            self.status_var.set("写入失败: " + (lines[0] if lines else "无响应"))

    def _default(self):
        self.timeout_var.set("500")
        self.temp_var.set("8000")
        self.period_var.set("1000")
        self.mask_var.set("0x0000000F")
        self.status_var.set("已填入默认值，点击“写入”下发")

    def refresh(self):
        pass


# ---------------------------------------------------------------- 4. Fault
class FaultPanel:
    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook)
        self._build()

    def _build(self):
        f = self.frame
        top = ttk.Frame(f)
        top.pack(fill="x", padx=8, pady=6)
        ttk.Button(top, text="刷新故障", command=self._refresh).pack(side="left", padx=4)
        ttk.Button(top, text="清空故障", command=self._clear).pack(side="left", padx=4)
        self.cnt_var = tk.StringVar(value="故障数: 0")
        ttk.Label(top, textvariable=self.cnt_var).pack(side="left", padx=10)

        g = ttk.LabelFrame(f, text="故障历史 / Fault History (GET_FAULT)", padding=8)
        g.pack(fill="both", expand=True, padx=8, pady=6)
        cols = ("ts", "code", "node", "param")
        self.tree = ttk.Treeview(g, columns=cols, show="headings", height=12)
        for c, t in zip(cols, ("时间戳(tick)", "故障码", "节点", "参数")):
            self.tree.heading(c, text=t)
            self.tree.column(c, width=120, anchor="center")
        self.tree.pack(fill="both", expand=True)

    def _refresh(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        lines = self.app.comm.query(P.CMD_GET_FAULT)
        self.app.model.update_faults(P.parse_fault(lines))
        self.refresh()

    def _clear(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        lines = self.app.comm.query(P.CMD_CLEAR_FAULT)
        if lines and lines[0].startswith("OK"):
            self.app.model.update_faults({"count": 0, "events": []})
            self.refresh()
            messagebox.showinfo("完成", "故障记录已清空")

    def refresh(self):
        s = self.app.model.snapshot()
        self.cnt_var.set(f"故障数: {len(s['faults'])}")
        self.tree.delete(*self.tree.get_children())
        for ev in s["faults"]:
            self.tree.insert("", "end",
                             values=[ev["ts"], ev["code"], ev["node"], ev["param"]])


# ---------------------------------------------------------------- 5. Logging
class LoggingPanel:
    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook)
        self._build()

    def _build(self):
        f = self.frame
        top = ttk.Frame(f)
        top.pack(fill="x", padx=8, pady=6)
        self.rec_var = tk.StringVar(value="开始记录")
        ttk.Button(top, textvariable=self.rec_var, command=self._toggle_rec
                   ).pack(side="left", padx=4)
        ttk.Label(top, text="节点:").pack(side="left", padx=(10, 2))
        self.node_var = tk.StringVar(value="1")
        self.node_cb = ttk.Combobox(top, textvariable=self.node_var, width=8,
                                    values=[str(i) for i in range(1, 5)],
                                    state="readonly")
        self.node_cb.pack(side="left")
        self.file_var = tk.StringVar(value="—")
        ttk.Label(top, text="文件:").pack(side="left", padx=(10, 2))
        ttk.Label(top, textvariable=self.file_var).pack(side="left")

        mid = ttk.LabelFrame(f, text="趋势图 / Trend (温度℃ & 电压V)", padding=8)
        mid.pack(fill="both", expand=True, padx=8, pady=6)
        self.chart = LineChart(mid, width=560, height=300)
        self.chart.pack(fill="both", expand=True)
        ttk.Label(mid, text="CSV 即记录文件，停止记录后即可用 Excel 打开",
                  foreground="#888").pack(anchor="w")

    def _toggle_rec(self):
        if self.app.logger.recording:
            self.app.logger.stop()
            self.rec_var.set("开始记录")
            self.file_var.set("—")
        else:
            path = CsvLogger.default_path()
            self.app.logger.start(path)
            self.rec_var.set("停止记录")
            self.file_var.set(path)

    def refresh(self):
        s = self.app.model.snapshot()
        try:
            nid = int(self.node_var.get())
        except ValueError:
            nid = 1
        hist = s["history"].get(nid, [])
        self.chart.set_data([
            {"name": "temp", "color": "#e74c3c",
             "points": [(t, v) for (t, v, _) in hist]},
            {"name": "volt", "color": "#2980b9",
             "points": [(t, v) for (t, _, v) in hist]},
        ])


# ---------------------------------------------------------------- 6. Maintenance
class MaintenancePanel:
    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook)
        self._build()

    def _build(self):
        f = self.frame
        top = ttk.Frame(f)
        top.pack(fill="x", padx=8, pady=6)
        ttk.Button(top, text="读取版本", command=lambda: self._cmd(P.CMD_GET_VERSION, "version")
                   ).pack(side="left", padx=4)
        ttk.Button(top, text="系统信息", command=lambda: self._cmd(P.CMD_GET_SYS, "sys")
                   ).pack(side="left", padx=4)
        ttk.Button(top, text="清计数器", command=lambda: self._cmd(P.CMD_CLEAR_COUNTERS, None)
                   ).pack(side="left", padx=4)
        ttk.Button(top, text="清故障", command=lambda: self._cmd(P.CMD_CLEAR_FAULT, None)
                   ).pack(side="left", padx=4)
        ttk.Button(top, text="复位设备", command=self._reset
                   ).pack(side="left", padx=4)

        # ---- P2 active push controls ----
        pf = ttk.LabelFrame(f, text="主动推送 (P2) / Active Push", padding=8)
        pf.pack(fill="x", padx=8, pady=4)
        self.push_en = tk.BooleanVar()
        self.push_period = tk.IntVar(value=1000)
        prow = ttk.Frame(pf)
        prow.pack(fill="x")
        ttk.Checkbutton(prow, text="启用推送", variable=self.push_en).pack(side="left", padx=4)
        ttk.Label(prow, text="周期(ms):").pack(side="left", padx=4)
        ttk.Spinbox(prow, from_=100, to=60000, increment=100,
                    textvariable=self.push_period, width=8).pack(side="left", padx=4)
        ttk.Button(prow, text="读取", command=self._read_push).pack(side="left", padx=4)
        ttk.Button(prow, text="保存", command=self._save_push).pack(side="left", padx=4)
        self.push_state = tk.StringVar(value="推送: 关闭")
        ttk.Label(pf, textvariable=self.push_state, wraplength=720,
                  justify="left").pack(fill="x", padx=4, pady=2)

        g = ttk.LabelFrame(f, text="固件/硬件信息 / Version & Sys Info", padding=8)
        g.pack(fill="both", expand=True, padx=8, pady=6)
        self.info = scrolledtext.ScrolledText(g, height=16, state="disabled")
        self.info.pack(fill="both", expand=True)

    def _cmd(self, cmd, kind):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        lines = self.app.comm.query(cmd)
        if kind == "version":
            self.app.model.update_version(P.parse_version(lines))
        elif kind == "sys":
            self.app.model.update_sys(lines)
        self._show(lines)

    def _reset(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        if not messagebox.askyesno("确认", "确定要复位设备吗？"):
            return
        lines = self.app.comm.query(P.CMD_RESET_DEVICE)
        self._show(lines)

    def _read_push(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        lines = self.app.comm.query(P.CMD_GET_PUSH)
        d = P.parse_push_cfg(lines)
        if d:
            self.app.model.update_push_cfg(d)
            self.push_en.set(d.get("enable", False))
            self.push_period.set(d.get("period_ms", 1000))
        self._show(lines)

    def _save_push(self):
        if not self.app.comm.is_open:
            messagebox.showwarning("未连接", "请先连接串口")
            return
        en = 1 if self.push_en.get() else 0
        period = self.push_period.get()
        cmd = f"{P.CMD_SET_PUSH} {en} {period}"
        lines = self.app.comm.query(cmd)
        self._show(lines)
        self._read_push()  # refresh UI from device

    def _show(self, lines):
        self.info.configure(state="normal")
        self.info.delete("1.0", "end")
        for ln in lines:
            self.info.insert("end", ln + "\n")
        self.info.configure(state="disabled")

    def refresh(self):
        snap = self.app.model.snapshot()
        en = snap.get("push_enabled", False)
        period = snap.get("push_period_ms", 0)
        last = snap.get("push_last_ts", 0.0)
        nodes = snap.get("push_nodes", {})
        if en:
            age = (time.time() - last) if last else 999.0
            s = f"推送: 启用  周期={period}ms  最后推送:{age:.1f}s前"
            if nodes:
                parts = []
                for nid in sorted(nodes.keys()):
                    nd = nodes[nid]
                    parts.append(
                        f"N{nid}:{'在线' if nd.get('on') else '离线'}"
                        f" V={nd.get('volt', 0) / 100.0:.2f}V"
                        f" T={nd.get('temp', 0) / 100.0:.2f}C"
                        f" K1={nd.get('k1')} K2={nd.get('k2')}")
                s += "\n  " + "  ".join(parts)
            self.push_state.set(s)
        else:
            self.push_state.set("推送: 关闭")


def P_serial_ports(app):
    from serial_comm import SerialComm
    return SerialComm.list_ports()


# ---------------------------------------------------------------- 7. Perf
class PerfPanel:
    """FreeRTOS runtime performance dashboard (GET_PERF).

    Shows per-task CPU% / stack high-water-mark, queue peak occupancy,
    CAN/UART throughput and heap high-water mark.
    """

    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook)
        self._build()

    def _build(self):
        f = self.frame

        # task table
        tf = ttk.LabelFrame(f, text="任务 CPU / 栈 / FreeRTOS 性能", padding=8)
        tf.pack(fill="both", expand=True, padx=8, pady=6)
        cols = ("name", "prio", "cpu", "hwm")
        self.task_tree = ttk.Treeview(tf, columns=cols, show="headings", height=12)
        for c, t, w in zip(cols,
                           ("任务名", "优先级", "CPU%", "栈高水位(B)"),
                           (160, 70, 70, 110)):
            self.task_tree.heading(c, text=t)
            self.task_tree.column(c, width=w, anchor="center")
        self.task_tree.pack(fill="both", expand=True)
        # CPU% bar column is rendered via tag colors
        self.task_tree.tag_configure("hot", foreground="#e74c3c")
        self.task_tree.tag_configure("warm", foreground="#e67e22")
        self.task_tree.tag_configure("cool", foreground="#27ae60")

        # summary block
        sf = ttk.LabelFrame(f, text="队列峰值 / 速率 / 堆", padding=8)
        sf.pack(fill="x", padx=8, pady=6)

        self.qpeak_var = tk.StringVar(value="队列峰值: —")
        ttk.Label(sf, textvariable=self.qpeak_var).pack(anchor="w")

        self.rate_var = tk.StringVar(value="速率: —")
        ttk.Label(sf, textvariable=self.rate_var).pack(anchor="w")

        heap_row = ttk.Frame(sf)
        heap_row.pack(fill="x", pady=(4, 0))
        ttk.Label(heap_row, text="堆:").pack(side="left")
        self.heap_bar = ttk.Progressbar(heap_row, mode="determinate", maximum=100)
        self.heap_bar.pack(side="left", fill="x", expand=True, padx=6)
        self.heap_var = tk.StringVar(value="—")
        ttk.Label(heap_row, textvariable=self.heap_var, width=22).pack(side="left")

        self.ts_var = tk.StringVar(value="最后更新: —")
        ttk.Label(sf, textvariable=self.ts_var, foreground="#888").pack(
            anchor="w", pady=(4, 0))

    def refresh(self):
        s = self.app.model.snapshot()
        perf = s.get("perf")
        if not perf:
            self.qpeak_var.set("队列峰值: —")
            self.rate_var.set("速率: —")
            self.heap_var.set("—")
            self.ts_var.set("最后更新: —")
            return

        # tasks
        self.task_tree.delete(*self.task_tree.get_children())
        for t in perf.get("tasks", []):
            cpu = t.get("cpu_pct", 0)
            tag = "hot" if cpu >= 50 else ("warm" if cpu >= 20 else "cool")
            self.task_tree.insert(
                "", "end",
                values=[t.get("name", "?"),
                        t.get("prio", 0),
                        f"{cpu}%",
                        t.get("hwm_bytes", 0)],
                tags=(tag,))

        # queue peak
        q = perf.get("qpeak", {})
        self.qpeak_var.set(
            f"队列峰值: CAN-RX={q.get('canrx', 0)}  "
            f"Fault={q.get('fault', 0)}  Report={q.get('report', 0)}")

        # rate
        r = perf.get("rate", {})
        self.rate_var.set(
            f"速率: CAN-RX={r.get('canrx', 0)}/s  "
            f"CAN-TX={r.get('cantx', 0)}/s  "
            f"UART-TX={r.get('uarttx', 0)}B/s")

        # heap
        h = perf.get("heap", {})
        peak = h.get("peak_used", 0)
        total = h.get("total", 0)
        if total > 0:
            pct = int(peak * 100 / total)
            self.heap_bar["value"] = pct
            self.heap_var.set(f"{peak}/{total}B ({pct}%)")
        else:
            self.heap_bar["value"] = 0
            self.heap_var.set("—")

        upd = time.strftime("%H:%M:%S", time.localtime(s["last_update"])) \
            if s["last_update"] else "—"
        self.ts_var.set(f"最后更新: {upd}")
