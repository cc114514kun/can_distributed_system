# CAN 应用层通信协议 V2.0

项目：CAN 总线分布式采集与边缘网关系统
适用固件：STM32F103 采集节点、STM32F407 网关

## 1. 物理层

- Classic CAN 2.0B 扩展数据帧
- 当前实际波特率：125 kbit/s
- 应用帧固定 DLC=8
- 总线两端各安装 120Ω 终端电阻
- 多字节字段使用大端顺序

## 2. 29 位扩展 ID

ID 只携带稳定的仲裁和路由信息，序列号放在数据区。

| 位范围 | 宽度 | 字段 | 说明 |
|---|---:|---|---|
| bit28~26 | 3 | Priority | 0 最高、7 最低 |
| bit25~22 | 4 | DeviceType | 设备类别 |
| bit21~14 | 8 | NodeID | 唯一节点号 |
| bit13~6 | 8 | MsgType | 消息类型 |
| bit5~0 | 6 | SubType | 业务子类型 |

```c
ExtId = (Priority << 26)
      | (DeviceType << 22)
      | (NodeID << 14)
      | (MsgType << 6)
      | SubType;
```

设备类型：`0x0=GATEWAY`，`0x1=COLLECTOR`。节点地址 `0x00` 为网关，`0x01~0xFE` 为普通节点，`0xFF` 为下行广播地址。

| MsgType | 名称 | 默认优先级 |
|---:|---|---:|
| 0x01 | SENSOR | 3 |
| 0x02 | NODE_STATUS | 4 |
| 0x03 | HEARTBEAT | 4 |
| 0x10 | OVER_TEMP | 1 |
| 0x11 | BUS_ERROR | 1 |
| 0x20 | CONTROL | 2 |
| 0x21 | CONTROL_ACK | 2 |

## 3. 公共数据封装

所有 V2.0 应用帧固定 DLC=8：

| 字节 | 内容 |
|---:|---|
| 0~1 | 16 位消息序列号，大端 |
| 2~6 | 5 字节消息体，由 MsgType/SubType 定义 |
| 7 | CRC8(Data[0..6]) |

CRC8 使用多项式 `0x07`、初始值 `0x00`，无输入/输出反射和末尾异或。周期数据和异步事件使用独立序列流，防止告警插入造成采集丢包误判。

## 4. 周期传感器帧

ID：Priority=3、DeviceType=COLLECTOR、NodeID=节点地址、MsgType=SENSOR、SubType=0。Node 1 的固定扩展 ID 为 `0x0C404040`。

| 字节 | 内容 |
|---:|---|
| 0~1 | SensorSequence |
| 2~3 | 电压×100，uint16，大端 |
| 4~5 | 温度×100，int16，大端 |
| 6 bit0 | K1 |
| 6 bit1 | K2 |
| 6 bit7~2 | 节点故障状态低 6 位 |
| 7 | CRC8 |

## 5. 超温告警帧

ID 的 MsgType 为 `0x10`、Priority 为 1。

| 字节 | 内容 |
|---:|---|
| 0~1 | EventSequence |
| 2~3 | 当前温度×100，int16，大端 |
| 4 | 告警子码，0x01 表示超温 |
| 5~6 | 保留 |
| 7 | CRC8 |

## 6. CAN 控制器错误帧

ID 的 MsgType 为 `0x11`、Priority 为 1。

| 字节 | 内容 |
|---:|---|
| 0~1 | EventSequence |
| 2 | REC |
| 3 | TEC |
| 4 | LEC |
| 5 | Bus-Off 标志 |
| 6 | 保留 |
| 7 | CRC8 |

## 7. 下行 LED 控制帧

ID 中的 NodeID 表示目标节点，也可以使用 `0xFF` 广播。Priority=2、DeviceType=COLLECTOR、MsgType=CONTROL、SubType=0。

| 字节 | 内容 |
|---:|---|
| 0~1 | CommandSequence |
| 2 | Opcode，0x01=SET_LED |
| 3 | 控制值，0=关，1=开 |
| 4~6 | 保留 |
| 7 | CRC8 |

## 8. 网关处理顺序

1. CAN ISR 读取硬件 FIFO，将原始报文放入 FreeRTOS 队列。
2. 网关任务检查扩展帧、DLC、29 位 ID、设备类型和节点号。
3. 校验 CRC8，再按 MsgType/SubType 分派。
4. SENSOR 帧更新节点表、历史缓存和最后接收时间。
5. 仅使用 SENSOR 序列号统计周期数据丢包。
6. 告警和错误帧进入故障管理队列，不更新采集序列基线。

## 9. 兼容性

V2.0 与旧版 DLC=5 协议不兼容。F103 和 F407 必须同时升级并重新烧录；旧节点报文会被新版网关作为非法长度帧丢弃。
