"""
charts.py — lightweight Tkinter Canvas line chart (no matplotlib dependency).

Draws one or two series (temp / voltage) over a time window for the selected node.
"""
from __future__ import annotations

import tkinter as tk


class LineChart(tk.Canvas):
    def __init__(self, master, width=420, height=240, **kw):
        super().__init__(master, width=width, height=height,
                         bg="#ffffff", bd=1, relief="sunken", **kw)
        self.w = width
        self.h = height
        self.series = []   # list of dict(name, color, points[(x,y)...])

    def set_data(self, series):
        """
        series: list of dict(name, color, points=[(t, value), ...])
        t is a unix timestamp (float); we plot relative to the window.
        """
        self.series = series or []
        self._redraw()

    def _redraw(self):
        self.delete("all")
        pad_l, pad_r, pad_t, pad_b = 42, 10, 12, 22
        plot_w = self.w - pad_l - pad_r
        plot_h = self.h - pad_t - pad_b

        # axes
        self.create_line(pad_l, pad_t, pad_l, pad_t + plot_h, fill="#888")
        self.create_line(pad_l, pad_t + plot_h, pad_l + plot_w, pad_t + plot_h,
                         fill="#888")

        if not self.series:
            self.create_text(self.w / 2, self.h / 2, text="无数据 / No data",
                             fill="#aaa")
            return

        # global min/max across all series
        all_vals = [v for s in self.series for (_, v) in s["points"]]
        if not all_vals:
            self.create_text(self.w / 2, self.h / 2, text="无数据 / No data",
                             fill="#aaa")
            return
        vmin = min(all_vals)
        vmax = max(all_vals)
        if vmax - vmin < 1e-6:
            vmax += 1.0
            vmin -= 1.0
        vspan = vmax - vmin

        # time window
        all_t = [t for s in self.series for (t, _) in s["points"]]
        tmin, tmax = min(all_t), max(all_t)
        if tmax - tmin < 1e-6:
            tmax = tmin + 1.0
        tspan = tmax - tmin

        def x_of(t):
            return pad_l + ((t - tmin) / tspan) * plot_w

        def y_of(v):
            return pad_t + plot_h - ((v - vmin) / vspan) * plot_h

        # y gridlines + labels
        for i in range(5):
            v = vmin + (vspan * i / 4.0)
            y = y_of(v)
            self.create_line(pad_l, y, pad_l + plot_w, y, fill="#eee")
            self.create_text(pad_l - 4, y, text=f"{v:.1f}", anchor="e",
                             fill="#666", font=("TkDefaultFont", 8))

        # x label (relative seconds)
        self.create_text(pad_l + plot_w, pad_t + plot_h + 12,
                         text=f"+{(tmax - tmin):.0f}s", anchor="e",
                         fill="#666", font=("TkDefaultFont", 8))

        # series lines
        for s in self.series:
            pts = s["points"]
            if len(pts) < 2:
                if pts:
                    x, y = x_of(pts[0][0]), y_of(pts[0][1])
                    self.create_oval(x - 2, y - 2, x + 2, y + 2,
                                     fill=s["color"], outline=s["color"])
                continue
            coords = []
            for (t, v) in pts:
                coords.append(x_of(t))
                coords.append(y_of(v))
            self.create_line(*coords, fill=s["color"], width=1.5,
                             smooth=False)
