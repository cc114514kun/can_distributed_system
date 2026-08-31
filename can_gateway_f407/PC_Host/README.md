# CAN 网关上位机工具（PC Host Software）

基于 `上位机实现要求.docx` 实现的工业 CAN 网关上位机，与 STM32F407 网关通过 UART（USB-TTL）
维护协议通信。覆盖文档要求的 6 大功能模块。

## 功能模块（对应 docx）

| 模块 | Tab | 功能 |
|---|---|---|
| 1 系统通信 | 通信 | 串口枚举/连接/断开、命令收发、调试控制台、通信错误统计 |
| 2 设备监控 | 监控 | 网关 CAN 状态、节点在线/离线、传感器实时数据（电压/温度/按键）、通信统计 |
| 3 设备配置 | 配置 | 读取/修改/保存参数（离线超时、温度告警上限、采样周期、节点使能掩码）到 Flash |
| 4 故障诊断 | 故障 | 故障历史展示、清空故障记录 |
| 5 数据采集与日志 | 日志 | 启动/停止 CSV 记录、温度/电压趋势图、导出 CSV |
| 6 系统维护 | 维护 | 版本/系统信息、清计数器、清故障、设备复位 |

## 运行环境

- Python 3.8+
- 依赖：`pyserial`（`tkinter` 为标准库自带）

```bash
pip install -r requirements.txt
python host_tool.py
```

## 使用步骤

1. 硬件：F407 网关的 UART（USART1，TTL）经 USB-TTL 转接板接到 PC。
2. 打开「通信」页，选择端口与波特率（默认 115200），点「连接」。
3. 「监控」页自动 1s 轮询刷新节点与传感器数据。
4. 「配置」页：点「读取」载入当前参数，修改后点「写入」（带 CRC8 校验、存 Flash）。
5. 「故障」页：查看/清空故障历史。
6. 「日志」页：点「开始记录」采样传感器并写 CSV，趋势图实时显示选中节点曲线；停止后 CSV 可在 Excel 打开。
7. 「维护」页：读取版本、清计数器、清故障、复位设备。

## 与下位机协议

见 `protocol_spec.md`。命令集（文本行，CRLF 结尾）：

```
GET_STATUS / GET_NODE / GET_SENSOR / GET_FAULT / CLEAR_FAULT
GET_VERSION / GET_SYS
GET_CONFIG / SET_CONFIG <timeout> <temp_hi_q100> <period> <mask_hex> [crc]
CLEAR_COUNTERS / RESET_DEVICE
```

下位机实现：`USER/APP/app_uart_cli.c`（本次已增补 GET_CONFIG / SET_CONFIG /
CLEAR_COUNTERS / RESET_DEVICE 四个命令）。

## 目录结构

```
PC_Host/
├── host_tool.py        # 主程序（GUI + 轮询线程）
├── serial_comm.py      # 串口通信层
├── protocol.py         # 命令构造 / 响应解析 / CRC8
├── data_model.py       # 共享数据模型（线程安全）
├── csv_logger.py       # CSV 日志
├── charts.py           # Canvas 趋势图（无 matplotlib 依赖）
├── panels.py           # 6 个功能 Tab
├── protocol_spec.md    # F407↔PC UART 协议规范
├── requirements.txt
└── README.md
```

## 已打包 exe（免 Python 直接运行）

桌面已生成单文件可执行程序 **`CAN_Host_Tool.exe`**，双击即可运行，无需安装 Python / pyserial。

- 位置：`C:\Users\qin\Desktop\CAN_Host_Tool.exe`（单文件 ~10 MB，自带 Tcl/Tk 与 pyserial）
- 运行时会额外打开一个**黑色控制台窗口**，用于实时打印串口收发与异常日志，方便排错（调试工具建议保留）。
- 若想要纯净无控制台窗口：用 `build_exe.bat` 重新打包，把 `--console` 改为 `--noconsole`。

### 重新打包（如需修改源码后重建）

双击 `PC_Host/build_exe.bat` 即可（需本机已存在完整版 Python，含 tkinter）：
`C:\Users\qin\AppData\Local\Programs\Python\Python313\python.exe`（本环境已安装）。

```bat
REM build_exe.bat 等价于：
python -m PyInstaller --onefile --console --name CAN_Host_Tool ^
  --distpath "%USERPROFILE%\Desktop" host_tool.py
```

## 说明 / 已知限制

- 趋势图使用 Tkinter Canvas 绘制，零额外依赖；如需更丰富图表可后续接入 matplotlib。
- 配置写入后保存到 Flash（`AppConfig_SaveToFlash`）；运行时应用这些参数需固件侧
  `App_NodeMonitorTask` / 告警逻辑读取 `g_sys_cfg`（后续增强项）。
- 本工具在 PC 端运行，需将 F407 固件重新编译烧录（含新增 CLI 命令）后方可联调。
