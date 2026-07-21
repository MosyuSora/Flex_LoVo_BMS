"""Desktop UART upper computer for the Zephyr BMS SNAP diagnostic stream."""

from __future__ import annotations

import csv
import threading
import time
import tkinter as tk
from collections import deque
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText
from typing import Any

import serial
from serial.tools import list_ports

from uart_protocol import (
    ACTIVE_CELL_CHANNELS,
    AFE_REGISTER_NAMES,
    cell_map,
    parse_afe_reg_line,
    parse_snap_line,
)


ROOT = Path(__file__).resolve().parents[2]
REPORT_DIR = ROOT / "tests" / "reports"


class EventRingBuffer:
    """Bounded producer/consumer buffer for UART events."""

    def __init__(self, maxlen: int = 4096):
        self._items: deque[tuple[str, Any]] = deque(maxlen=maxlen)
        self._maxlen = maxlen
        self._lock = threading.Lock()
        self._dropped = 0

    def put(self, item: tuple[str, Any]) -> None:
        with self._lock:
            if len(self._items) >= self._maxlen:
                self._items.popleft()
                self._dropped += 1
            self._items.append(item)

    def drain(self, limit: int) -> list[tuple[str, Any]]:
        with self._lock:
            count = min(max(0, limit), len(self._items))
            return [self._items.popleft() for _ in range(count)]

    def take_dropped(self) -> int:
        with self._lock:
            dropped = self._dropped
            self._dropped = 0
            return dropped

    def __bool__(self) -> bool:
        with self._lock:
            return bool(self._items)


class SerialReader(threading.Thread):
    def __init__(self, connection: serial.Serial, events: EventRingBuffer):
        super().__init__(daemon=True)
        self.connection = connection
        self.events = events
        self.stop_event = threading.Event()

    def stop(self) -> None:
        self.stop_event.set()

    def run(self) -> None:
        try:
            while not self.stop_event.is_set():
                raw = self.connection.readline()
                if raw:
                    self.events.put(("line", raw.decode("utf-8", errors="replace")))
        except Exception as exc:  # serial exceptions are surfaced in the UI
            if not self.stop_event.is_set():
                self.events.put(("error", str(exc)))


class UartUpperComputer:
    def __init__(self, root: tk.Tk, initial_port: str = "", initial_baud: int = 115200):
        self.root = root
        self.root.title("BMS UART 上位机")
        self.root.geometry("1320x860")
        self.root.minsize(1060, 700)

        self.events = EventRingBuffer(maxlen=4096)
        self.connection: serial.Serial | None = None
        self.reader: SerialReader | None = None
        self.raw_lines: deque[str] = deque(maxlen=3000)
        self.snapshots: deque[dict[str, Any]] = deque(maxlen=3000)
        self.plot_cache: deque[tuple[float, float]] = deque(maxlen=6000)
        self.trace_lines: deque[str] = deque(maxlen=500)
        self.trace_dirty = False
        self.render_dirty = False
        self.register_dirty = False
        self.dropped_event_total = 0
        self.plot_window_seconds = 30.0
        self.current: dict[str, Any] | None = None
        self.last_snapshot_time = 0.0
        self.afe_registers: dict[int, int] = {}
        self.register_window: tk.Toplevel | None = None
        self.register_tree: ttk.Treeview | None = None
        self.register_rows: dict[int, str] = {}

        self.port_var = tk.StringVar(value=initial_port)
        self.baud_var = tk.StringVar(value=str(initial_baud))
        self.status_var = tk.StringVar(value="未连接")
        self.counter_var = tk.StringVar(value="SNAP 0")
        self.last_update_var = tk.StringVar(value="等待串口数据")
        self.auto_request_var = tk.BooleanVar(value=False)
        self.interval_var = tk.StringVar(value="500")
        self.chart_auto_range_var = tk.BooleanVar(value=True)
        self.chart_min_var = tk.StringVar(value="0.0")
        self.chart_max_var = tk.StringVar(value="60.0")

        self.metric_vars = {key: tk.StringVar(value="-") for key in (
            "pack", "current", "soc", "soh", "temp", "power", "fault", "sample"
        )}
        self.cell_vars = {channel: tk.StringVar(value="-") for channel in range(1, 16)}
        self.cell_widgets: dict[int, tk.Frame] = {}
        self.cell_value_labels: dict[int, tk.Label] = {}
        self.cell_balance_vars = {channel: tk.StringVar(value="-") for channel in range(1, 16)}
        self.balance_status_var = tk.StringVar(value="均衡状态：等待 SNAP")

        self._configure_style()
        self._build_ui()
        self.refresh_ports()
        self.root.after(80, self._drain_events)
        self.root.after(250, self._auto_request_tick)
        self.root.after(2000, self._refresh_ports_tick)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _configure_style(self) -> None:
        style = ttk.Style(self.root)
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 20, "bold"), foreground="#172027")
        style.configure("Section.TLabel", font=("Microsoft YaHei UI", 13, "bold"), foreground="#263640")
        style.configure("Metric.TLabel", font=("Microsoft YaHei UI", 20, "bold"), foreground="#172027")
        style.configure("Muted.TLabel", foreground="#66737c")
        style.configure("Primary.TButton", padding=(12, 6))

    def _build_ui(self) -> None:
        self.root.configure(bg="#f4f6f7")
        header = tk.Frame(self.root, bg="#263640", padx=18, pady=14)
        header.pack(fill="x")
        tk.Label(header, text="BMS UART 上位机", bg="#263640", fg="white",
                 font=("Microsoft YaHei UI", 20, "bold")).pack(side="left")
        tk.Label(header, textvariable=self.status_var, bg="#263640", fg="#b9e5cf",
                 font=("Microsoft YaHei UI", 11)).pack(side="right", padx=6)

        controls = ttk.Frame(self.root, padding=(18, 12, 18, 6))
        controls.pack(fill="x")
        ttk.Label(controls, text="串口").pack(side="left")
        self.port_box = ttk.Combobox(controls, textvariable=self.port_var, width=16, state="readonly")
        self.port_box.pack(side="left", padx=(6, 14))
        ttk.Button(controls, text="刷新", command=self.refresh_ports).pack(side="left")
        ttk.Label(controls, text="波特率").pack(side="left", padx=(18, 6))
        ttk.Combobox(controls, textvariable=self.baud_var, width=10, state="readonly",
                     values=("9600", "38400", "115200", "230400")).pack(side="left")
        self.connect_button = ttk.Button(controls, text="连接", style="Primary.TButton", command=self.toggle_connection)
        self.connect_button.pack(side="left", padx=(18, 6))
        ttk.Button(controls, text="读取 AFE 寄存器", command=lambda: self.send_command("R")).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="查看 AFE 寄存器", command=self.open_register_window).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="请求 SNAP", command=lambda: self.send_command("S")).pack(side="left")
        ttk.Checkbutton(controls, text="自动请求", variable=self.auto_request_var).pack(side="left", padx=(18, 4))
        ttk.Label(controls, text="周期 ms").pack(side="left")
        ttk.Entry(controls, textvariable=self.interval_var, width=7).pack(side="left", padx=(4, 14))
        ttk.Button(controls, text="保存 CSV", command=self.save_csv).pack(side="right", padx=4)
        ttk.Button(controls, text="保存原始日志", command=self.save_raw).pack(side="right")

        body = ttk.Frame(self.root, padding=(18, 4, 18, 18))
        body.pack(fill="both", expand=True)
        self._build_metrics(body)
        self._build_cells(body)
        self._build_bottom(body)

    def _build_metrics(self, parent: ttk.Frame) -> None:
        frame = ttk.LabelFrame(parent, text="实时状态", padding=10)
        frame.pack(fill="x", pady=(0, 10))
        metrics = (
            ("pack", "Pack 电压", "V"), ("current", "电流", "A"),
            ("soc", "SOC", "%"), ("soh", "SOH", "%"),
            ("temp", "温度", "°C"), ("power", "功率状态", ""),
            ("fault", "保护状态", ""), ("sample", "采样版本", ""),
        )
        for index, (key, title, suffix) in enumerate(metrics):
            card = tk.Frame(frame, bg="#ffffff", highlightbackground="#d5dce0", highlightthickness=1)
            card.grid(row=0, column=index, sticky="nsew", padx=4)
            frame.columnconfigure(index, weight=1)
            tk.Label(card, text=title, bg="#ffffff", fg="#66737c",
                     font=("Microsoft YaHei UI", 10)).pack(anchor="w", padx=10, pady=(8, 0))
            row = tk.Frame(card, bg="#ffffff")
            row.pack(anchor="w", padx=10, pady=(0, 8))
            tk.Label(row, textvariable=self.metric_vars[key], bg="#ffffff", fg="#172027",
                     font=("Microsoft YaHei UI", 18, "bold")).pack(side="left")
            if suffix:
                tk.Label(row, text=suffix, bg="#ffffff", fg="#66737c",
                         font=("Microsoft YaHei UI", 10)).pack(side="left", padx=(4, 0), pady=(8, 0))

    def _build_cells(self, parent: ttk.Frame) -> None:
        frame = ttk.LabelFrame(parent, text="15 串槽位 / 实际 9 串", padding=10)
        frame.pack(fill="x", pady=(0, 10))
        ttk.Label(frame, textvariable=self.balance_status_var).grid(
            row=0, column=0, columnspan=15, sticky="w", pady=(0, 6)
        )
        for channel in range(1, 16):
            active = channel in ACTIVE_CELL_CHANNELS
            card = tk.Frame(frame, bg="#e4f3ec" if active else "#eef1f3",
                            highlightbackground="#16865b" if active else "#d5dce0",
                            highlightthickness=1)
            card.grid(row=1, column=channel - 1, sticky="nsew", padx=2)
            frame.columnconfigure(channel - 1, weight=1)
            tk.Label(card, text=f"VC{channel}", bg=card["bg"], fg="#263640",
                     font=("Microsoft YaHei UI", 9)).pack(pady=(5, 0))
            value_label = tk.Label(card, textvariable=self.cell_vars[channel], bg=card["bg"], fg="#172027",
                                   font=("Microsoft YaHei UI", 12, "bold"))
            value_label.pack(pady=(1, 0))
            tk.Label(card, textvariable=self.cell_balance_vars[channel], bg=card["bg"],
                     fg="#16865b", font=("Microsoft YaHei UI", 8, "bold")).pack(pady=(0, 1))
            tk.Label(card, text="已接入" if active else "未接入", bg=card["bg"], fg="#66737c",
                     font=("Microsoft YaHei UI", 8)).pack(pady=(0, 5))
            self.cell_widgets[channel] = card
            self.cell_value_labels[channel] = value_label

    def _build_bottom(self, parent: ttk.Frame) -> None:
        lower = ttk.Frame(parent)
        lower.pack(fill="both", expand=True)
        trend = ttk.LabelFrame(lower, text="电压趋势", padding=8)
        trend.pack(side="left", fill="both", expand=True, padx=(0, 8))
        self.chart = tk.Canvas(trend, bg="#ffffff", height=220, highlightthickness=0)
        self.chart.pack(fill="both", expand=True)
        chart_controls = ttk.Frame(trend)
        chart_controls.pack(fill="x", pady=(0, 6), before=self.chart)
        ttk.Label(chart_controls, text="最近 30 s").pack(side="left")
        ttk.Checkbutton(chart_controls, text="自动纵轴", variable=self.chart_auto_range_var,
                        command=self._draw_trend).pack(side="left", padx=(14, 8))
        ttk.Label(chart_controls, text="最小 V").pack(side="left")
        ttk.Entry(chart_controls, textvariable=self.chart_min_var, width=8).pack(side="left", padx=(4, 8))
        ttk.Label(chart_controls, text="最大 V").pack(side="left")
        ttk.Entry(chart_controls, textvariable=self.chart_max_var, width=8).pack(side="left", padx=(4, 8))
        ttk.Button(chart_controls, text="应用", command=self._apply_chart_range).pack(side="left")
        self.chart.bind("<Configure>", lambda _event: self._draw_trend())
        trace = ttk.LabelFrame(lower, text="UART 原始报文", padding=8)
        trace.pack(side="left", fill="both", expand=True)
        self.trace = ScrolledText(trace, height=10, wrap="none", font=("Consolas", 9), state="disabled")
        self.trace.pack(fill="both", expand=True)
        footer = ttk.Frame(parent)
        footer.pack(fill="x", pady=(8, 0))
        ttk.Label(footer, textvariable=self.counter_var, style="Muted.TLabel").pack(side="left")
        ttk.Label(footer, textvariable=self.last_update_var, style="Muted.TLabel").pack(side="right")

    def refresh_ports(self) -> None:
        ports = [item.device for item in list_ports.comports()]
        self.port_box["values"] = ports
        if self.port_var.get() not in ports:
            self.port_var.set(ports[0] if ports else "")
        if not ports:
            self.status_var.set("未发现串口")

    def _refresh_ports_tick(self) -> None:
        """Pick up USB/IP serial devices that appear after the window starts."""
        if self.connection is None:
            self.refresh_ports()
        self.root.after(2000, self._refresh_ports_tick)

    def toggle_connection(self) -> None:
        if self.connection is not None:
            self.disconnect()
            return
        if not self.port_var.get():
            messagebox.showwarning("串口", "请先选择串口")
            return
        try:
            self.connection = serial.Serial(self.port_var.get(), int(self.baud_var.get()), timeout=0.2)
            self.reader = SerialReader(self.connection, self.events)
            self.reader.start()
            self.connect_button.configure(text="断开")
            self.status_var.set(f"已连接 {self.port_var.get()}")
            self.send_command("S")
        except (ValueError, serial.SerialException) as exc:
            self.connection = None
            messagebox.showerror("串口连接失败", str(exc))

    def disconnect(self) -> None:
        if self.reader:
            self.reader.stop()
        if self.connection:
            try:
                self.connection.close()
            except serial.SerialException:
                pass
        self.reader = None
        self.connection = None
        self.connect_button.configure(text="连接")
        self.status_var.set("未连接")

    def send_command(self, command: str) -> None:
        if self.connection is None or not self.connection.is_open:
            return
        try:
            self.connection.write(command.encode("ascii"))
        except serial.SerialException as exc:
            self.events.put(("error", str(exc)))

    def _auto_request_tick(self) -> None:
        if self.auto_request_var.get() and self.connection is not None:
            self.send_command("S")
        try:
            interval = max(100, int(self.interval_var.get()))
        except ValueError:
            interval = 500
        self.root.after(interval if self.auto_request_var.get() else 250, self._auto_request_tick)

    def _drain_events_legacy(self) -> None:
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "line":
                    self._handle_line(value)
                elif kind == "error":
                    self.status_var.set("串口异常")
                    self._append_trace("ERROR " + str(value))
        except queue.Empty:
            pass
        self.root.after(80, self._drain_events)

    def _drain_events(self) -> None:
        for kind, value in self.events.drain(300):
            if kind == "line":
                self._handle_line(value)
            elif kind == "error":
                self.status_var.set("涓插彛寮傚父")
                self._append_trace("ERROR " + str(value))

        dropped = self.events.take_dropped()
        if dropped:
            self.dropped_event_total += dropped
            self.status_var.set(
                f"UART 缓冲已丢弃 {self.dropped_event_total} 条旧报文"
            )
        if self.register_dirty:
            self._refresh_register_tree()
            self.register_dirty = False
        if self.render_dirty and self.current is not None:
            self._render_snapshot(self.current)
            self.render_dirty = False
        if self.trace_dirty:
            self._flush_trace()
        self.root.after(10 if self.events else 80, self._drain_events)

    def _handle_line(self, line: str) -> None:
        clean = line.rstrip("\r\n")
        self.raw_lines.append(clean)
        self._append_trace(clean)
        register = parse_afe_reg_line(clean)
        if register is not None:
            address, value = register
            self.afe_registers[address] = value
            self.register_dirty = True
            return
        snapshot = parse_snap_line(clean)
        if snapshot is None:
            return
        self.current = snapshot
        self.snapshots.append(snapshot)
        self.last_snapshot_time = time.time()
        self.plot_cache.append((self.last_snapshot_time, float(snapshot.get("pack_v", 0.0))))
        self.render_dirty = True

    def _append_trace(self, line: str) -> None:
        self.trace_lines.append(line)
        self.trace_dirty = True

    def _flush_trace(self) -> None:
        """Paint the bounded trace buffer once per event batch."""
        self.trace.configure(state="normal")
        self.trace.delete("1.0", "end")
        if self.trace_lines:
            self.trace.insert("end", "\n".join(self.trace_lines) + "\n")
        self.trace.see("end")
        self.trace.configure(state="disabled")
        self.trace_dirty = False

    def _render_snapshot(self, snapshot: dict[str, Any]) -> None:
        self.metric_vars["pack"].set(f"{snapshot['pack_v']:.3f}")
        self.metric_vars["current"].set(f"{snapshot['current_a']:.3f}")
        self.metric_vars["soc"].set(f"{snapshot['soc_percent']:.1f}")
        self.metric_vars["soh"].set(f"{snapshot['soh_percent']:.1f}")
        temps = ", ".join(f"{value:.2f}" for value in snapshot.get("temp_c", []))
        self.metric_vars["temp"].set(temps or "-")
        self.metric_vars["power"].set(snapshot.get("power_name", "未知"))
        faults = snapshot.get("fault_names", [])
        self.metric_vars["fault"].set("正常" if not faults else "、".join(faults))
        self.metric_vars["sample"].set(str(snapshot.get("version", "-")))

        mapped = cell_map(snapshot.get("cells_mv", []))
        for channel, value in mapped.items():
            self.cell_vars[channel].set("-" if value is None else f"{value} mV")
            color = "#b83e3e" if value is not None and (value < 2800 or value > 4200) else "#172027"
            self.cell_value_labels[channel].configure(fg=color)

        balance_mask = int(snapshot.get("bal_mask", 0))
        requested_mask = int(snapshot.get("bal_req", balance_mask))
        delta_mv = int(snapshot.get("bal_delta_mv", 0))
        balancing_channels = []
        for channel in range(1, 16):
            if channel not in ACTIVE_CELL_CHANNELS:
                self.cell_balance_vars[channel].set("-")
                continue
            logical_index = ACTIVE_CELL_CHANNELS.index(channel)
            enabled = (balance_mask & (1 << logical_index)) != 0
            self.cell_balance_vars[channel].set("均衡中" if enabled else "关闭")
            if enabled:
                balancing_channels.append(f"VC{channel}")
        active_text = ",".join(balancing_channels) if balancing_channels else "无"
        self.balance_status_var.set(
            f"均衡：实际 {active_text} | 请求 0x{requested_mask:04X} | 压差 {delta_mv} mV"
        )

        self.counter_var.set(f"SNAP {len(self.snapshots)}")
        self.last_update_var.set(f"最近更新 {time.strftime('%H:%M:%S')}")
        self._draw_trend()

    def open_register_window(self) -> None:
        if self.register_window is not None and self.register_window.winfo_exists():
            self.register_window.deiconify()
            self.register_window.lift()
            return

        window = tk.Toplevel(self.root)
        window.title("BQ76940 AFE 寄存器")
        window.geometry("560x700")
        window.minsize(480, 500)
        self.register_window = window
        window.protocol("WM_DELETE_WINDOW", self._close_register_window)

        panel = ttk.Frame(window, padding=10)
        panel.pack(fill="both", expand=True)
        ttk.Label(panel, text="点击主窗口‘读取 AFE 寄存器’获取最新值").pack(anchor="w", pady=(0, 6))
        tree_frame = ttk.Frame(panel)
        tree_frame.pack(fill="both", expand=True)
        columns = ("address", "name", "hex", "decimal")
        self.register_tree = ttk.Treeview(tree_frame, columns=columns, show="headings")
        headings = {"address": "地址", "name": "寄存器", "hex": "HEX", "decimal": "DEC"}
        widths = {"address": 80, "name": 210, "hex": 100, "decimal": 100}
        for column in columns:
            self.register_tree.heading(column, text=headings[column])
            self.register_tree.column(column, width=widths[column], anchor="center")
        scrollbar = ttk.Scrollbar(tree_frame, orient="vertical", command=self.register_tree.yview)
        self.register_tree.configure(yscrollcommand=scrollbar.set)
        self.register_tree.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.register_rows.clear()
        for address in range(0x5A):
            name = AFE_REGISTER_NAMES.get(address, "RESERVED")
            row = self.register_tree.insert(
                "", "end", values=(f"0x{address:02X}", name, "--", "--")
            )
            self.register_rows[address] = row
        self._refresh_register_tree()
        self.send_command("R")

    def _close_register_window(self) -> None:
        if self.register_window is not None:
            self.register_window.destroy()
        self.register_window = None
        self.register_tree = None
        self.register_rows.clear()

    def _refresh_register_tree(self) -> None:
        if self.register_tree is None or not self.register_tree.winfo_exists():
            return
        for address, row in self.register_rows.items():
            value = self.afe_registers.get(address)
            self.register_tree.item(
                row,
                values=(
                    f"0x{address:02X}",
                    AFE_REGISTER_NAMES.get(address, "RESERVED"),
                    "--" if value is None else f"0x{value:02X}",
                    "--" if value is None else str(value),
                ),
            )

    def _apply_chart_range(self) -> None:
        try:
            low = float(self.chart_min_var.get())
            high = float(self.chart_max_var.get())
        except ValueError:
            messagebox.showwarning("绘图范围", "纵轴范围必须是数字")
            return
        if high <= low:
            messagebox.showwarning("绘图范围", "最大值必须大于最小值")
            return
        self.chart_auto_range_var.set(False)
        self._draw_trend()

    def _draw_trend(self) -> None:
        self.chart.delete("all")
        width = max(300, self.chart.winfo_width())
        height = max(160, self.chart.winfo_height())
        left, right, top, bottom = 46, 16, 16, 28
        self.chart.create_rectangle(left, top, width - right, height - bottom, outline="#d5dce0")
        now = time.time()
        while self.plot_cache and now - self.plot_cache[0][0] > self.plot_window_seconds:
            self.plot_cache.popleft()
        values = list(self.plot_cache)
        if len(values) < 2:
            self.chart.create_text(width / 2, height / 2, text="等待 SNAP 数据", fill="#66737c")
            return
        packs = [value for _timestamp, value in values]
        if self.chart_auto_range_var.get():
            low, high = min(packs), max(packs)
            if high - low < 0.01:
                low -= 0.1
                high += 0.1
            self.chart_min_var.set(f"{low:.2f}")
            self.chart_max_var.set(f"{high:.2f}")
        else:
            try:
                low = float(self.chart_min_var.get())
                high = float(self.chart_max_var.get())
            except ValueError:
                return
            if high <= low:
                return
        span = high - low
        for ratio in (0.0, 0.5, 1.0):
            y = top + (height - top - bottom) * (1.0 - ratio)
            label = low + (high - low) * ratio
            self.chart.create_line(left, y, width - right, y, fill="#eef1f3")
            self.chart.create_text(left - 8, y, text=f"{label:.2f}", anchor="e", fill="#66737c")
        points: list[float] = []
        for index, value in enumerate(packs):
            x = left + (width - left - right) * index / max(1, len(packs) - 1)
            ratio = max(0.0, min(1.0, (value - low) / span))
            y = top + (height - top - bottom) * (1.0 - ratio)
            points.extend((x, y))
        self.chart.create_line(*points, fill="#16865b", width=2, smooth=True)
        self.chart.create_text(left, height - 10, text="Pack 电压 (V)", anchor="w", fill="#66737c")

    def save_csv(self) -> None:
        if not self.snapshots:
            messagebox.showinfo("保存 CSV", "还没有解析到 SNAP 数据")
            return
        REPORT_DIR.mkdir(parents=True, exist_ok=True)
        path = filedialog.asksaveasfilename(
            title="保存 SNAP CSV", defaultextension=".csv",
            initialdir=str(REPORT_DIR),
            filetypes=(("CSV 文件", "*.csv"), ("所有文件", "*.*")),
        )
        if not path:
            return
        fields = ["version", "pack_mv", "current_ma", "soc_0p1", "soh_0p1", "fault_bits", "afe_status", "bal_mask", "pwr_mode", "chg", "dsg"]
        fields += [f"vc{channel}_mv" for channel in range(1, 16)]
        with open(path, "w", newline="", encoding="utf-8-sig") as output:
            writer = csv.DictWriter(output, fieldnames=fields)
            writer.writeheader()
            for snapshot in self.snapshots:
                row = {field: snapshot.get(field, "") for field in fields}
                row.update({f"vc{channel}_mv": value or "" for channel, value in cell_map(snapshot.get("cells_mv", [])).items()})
                writer.writerow(row)
        self.status_var.set(f"CSV 已保存: {Path(path).name}")

    def save_raw(self) -> None:
        if not self.raw_lines:
            messagebox.showinfo("保存日志", "还没有串口数据")
            return
        REPORT_DIR.mkdir(parents=True, exist_ok=True)
        path = filedialog.asksaveasfilename(
            title="保存 UART 原始日志", defaultextension=".log",
            initialdir=str(REPORT_DIR),
            filetypes=(("日志文件", "*.log"), ("所有文件", "*.*")),
        )
        if path:
            Path(path).write_text("\n".join(self.raw_lines) + "\n", encoding="utf-8")
            self.status_var.set(f"日志已保存: {Path(path).name}")

    def close(self) -> None:
        self.disconnect()
        self.root.destroy()


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="BMS UART SNAP upper computer")
    parser.add_argument("--port", default="", help="initial serial port, e.g. COM3")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    root = tk.Tk()
    UartUpperComputer(root, args.port, args.baud)
    root.mainloop()


if __name__ == "__main__":
    main()
