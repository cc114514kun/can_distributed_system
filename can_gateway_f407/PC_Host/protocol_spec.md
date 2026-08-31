# F407 ↔ PC 上位机 UART 维护协议规范（V1.0）

> 适用范围：STM32F407 网关 ↔ PC 上位机工具，通过 UART（USB-TTL，USART1）通信。
> 对应固件实现：`USER/APP/app_uart_cli.c`。
> 本文档定义接口，便于上位机与下位机分别维护、互不耦合。

---

## 1. 物理层

| 项目 | 值 |
|---|---|
| 接口 | UART（TTL 3.3V，建议经 USB-TTL 转接） |
| 波特率 | 115200（默认，可扩展） |
| 数据位 / 停止位 / 校验 | 8 / 1 / None |
| 流控 | 无 |

---

## 2. 帧格式（文本行协议）

上位机与下位机均以 **ASCII 文本行** 为单位交互，每条命令/响应以 `\r\n`（CRLF）结尾。

```
<命令或响应文本>\r\n
```

- 命令：由上位机发起，单行，全大写，参数以空格分隔。
- 响应：下位机回复，**首行** 以 `OK ` 或 `ERR ` 开头；多行数据响应在首行之后用空格缩进续行。
- 编码：UTF-8（ASCII 子集）。

### 2.1 多行响应约定

多行数据响应结构：

```
OK <HEADER>
  <field1>:value <field2>:value ...
  <field1>:value <field2>:value ...
```

上位机读取策略：发送命令后，持续读取直到无新数据（超时 ~300ms）或连接关闭，按行解析；首行 `ERR` 表示失败。

---

## 3. 命令集

### 3.1 监控类（对应文档模块 2）

| 命令 | 响应 | 说明 |
|---|---|---|
| `GET_STATUS` | `OK CAN Stat:Invalid:<u> BusOffRetry:<u>` | CAN 统计：非法帧数 / Bus-Off 重试次数 |
| `GET_NODE` | `OK NODE_LIST` + `  ID:<u> Seq:<u> Online:<u>` ×N | 节点在线/序列号 |
| `GET_SENSOR` | `OK SENSOR_DATA` + `  N<u> VoltQ100:<u> Temp:<d> Key:<u>` ×N | 实时传感器数据 |
| `GET_SYS` | `OK SYS_INFO` + 队列占用等 | 系统运行信息 |

### 3.2 故障类（模块 4）

| 命令 | 响应 | 说明 |
|---|---|---|
| `GET_FAULT` | `OK FAULT_CNT:<u>` + `  TS:<lu> CODE:<str> NODE:<d> PARAM:<lu>` ×n | 故障历史 |
| `CLEAR_FAULT` | `OK FAULT_CLEARED` | 清空故障记录 |

### 3.3 配置类（模块 3）

| 命令 | 响应 | 说明 |
|---|---|---|
| `GET_CONFIG` | `OK CONFIG TIMEOUT:<ms> TEMP_HI:<q100> PERIOD:<ms> NODEMASK:0x<hex>` | 读取当前配置 |
| `SET_CONFIG <timeout_ms> <temp_hi_q100> <period_ms> <node_mask_hex> [crc]` | `OK CONFIG_SAVED ...` / `ERR ...` | 写入并保存到 Flash |

字段含义：
- `TIMEOUT`：节点离线判定超时（ms），范围 50~60000。
- `TEMP_HI`：温度告警上限，Q100 定点（如 8000 = 80.00℃），范围 -4000~20000。
- `PERIOD`：采样/上报周期（ms），范围 50~60000。
- `NODEMASK`：节点使能掩码，bit0=node1 … bit3=node4，十六进制。

### 3.4 维护类（模块 6）

| 命令 | 响应 | 说明 |
|---|---|---|
| `GET_VERSION` | `OK VER:1.0.0_CAN_GW` 或产品信息多行 | 固件/硬件版本 |
| `GET_SYS` | 见 3.1 | 系统信息 |
| `CLEAR_COUNTERS` | `OK COUNTERS_CLEARED` | 清零 CAN 统计与节点计数/序列号 |
| `RESET_DEVICE` | `OK REBOOTING` → 软件复位 | 设备重启 |

### 3.5 其他

| 命令 | 响应 | 说明 |
|---|---|---|
| （任意未知命令） | `ERR Unknown cmd: <cmd>` | 命令不存在 |

---

## 4. CRC 校验（模块 1：CRC verification）

`SET_CONFIG` 支持可选的 CRC8 校验，用于保证配置载荷完整性：

- 算法：**CRC8，多项式 0x07，初始值 0x00**（与 CAN 传感器帧一致）。
- 校验载荷：将 4 个参数（`timeout_ms`、`temp_hi_q100`、`period_ms`、`node_mask`）各按 **小端 4 字节** 打包为 16 字节，`crc = CRC8(payload[0..15])`。
- 上位机在命令末位附加 `crc`（十六进制，1 字节）。
- 下位机收到后重算并比对，不一致返回 `ERR SET_CONFIG CRC mismatch`。

不附加 `crc` 时，下位机跳过校验（兼容调试）。

### 4.1 示例

写入 `timeout=500, temp_hi=8000, period=1000, mask=0x0000000F`：

```
SET_CONFIG 500 8000 1000 0000000F <crc>
```

下位机回 `OK CONFIG_SAVED TIMEOUT:500 TEMP_HI:8000 PERIOD:1000 NODEMASK:0x0000000F`。

---

## 5. 上位机轮询建议

- 监控面板：每 1s 轮询 `GET_STATUS` / `GET_NODE` / `GET_SENSOR` / `GET_FAULT`。
- 配置面板：进入时 `GET_CONFIG` 读取，修改后 `SET_CONFIG` 写回。
- 维护面板：按钮触发 `GET_VERSION` / `CLEAR_COUNTERS` / `RESET_DEVICE` / `CLEAR_FAULT`。
- 通信错误（超时无响应 / 收到 `ERR`）计入通信统计并在 UI 提示。

---

## 6. 扩展预留

- 后续可加入二进制帧（`0xAA` 帧头 + 长度 + CMD + 载荷 + CRC16）以支持更高吞吐；当前文本协议已满足维护/诊断场景。
- 节点数 `MAX_NODE_NUM = 4`（固件定义），上位机按响应行数自适应。
