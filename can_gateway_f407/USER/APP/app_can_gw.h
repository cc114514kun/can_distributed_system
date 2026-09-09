#ifndef __APP_CAN_GW_H
#define __APP_CAN_GW_H

#include "bsp_can.h"
#include <stdint.h>

/* CAN application protocol V2.0, 29-bit extended identifier:
 * [Priority:3][DeviceType:4][NodeID:8][MsgType:8][SubType:6]
 * Sequence numbers are carried in data[0..1]. */
#define CAN_ID_PRIORITY_SHIFT       26U
#define CAN_ID_DEVICE_TYPE_SHIFT    22U
#define CAN_ID_NODE_ID_SHIFT        14U
#define CAN_ID_MSG_TYPE_SHIFT        6U
#define CAN_ID_SUB_TYPE_SHIFT        0U

#define CAN_ID_PRIORITY_MASK        0x07U
#define CAN_ID_DEVICE_TYPE_MASK     0x0FU
#define CAN_ID_NODE_ID_MASK         0xFFU
#define CAN_ID_MSG_TYPE_MASK        0xFFU
#define CAN_ID_SUB_TYPE_MASK        0x3FU
#define CAN_EXT_ID_MASK             0x1FFFFFFFUL

#define CAN_MAKE_EXTID(priority, device_type, node_id, msg_type, sub_type) \
    (((((uint32_t)(priority))    & CAN_ID_PRIORITY_MASK)    << CAN_ID_PRIORITY_SHIFT)    | \
     ((((uint32_t)(device_type)) & CAN_ID_DEVICE_TYPE_MASK) << CAN_ID_DEVICE_TYPE_SHIFT) | \
     ((((uint32_t)(node_id))     & CAN_ID_NODE_ID_MASK)     << CAN_ID_NODE_ID_SHIFT)     | \
     ((((uint32_t)(msg_type))    & CAN_ID_MSG_TYPE_MASK)    << CAN_ID_MSG_TYPE_SHIFT)    | \
     ((((uint32_t)(sub_type))    & CAN_ID_SUB_TYPE_MASK)    << CAN_ID_SUB_TYPE_SHIFT))

#define CAN_ID_GET_PRIORITY(id)    (uint8_t)((((uint32_t)(id)) >> CAN_ID_PRIORITY_SHIFT)    & CAN_ID_PRIORITY_MASK)
#define CAN_ID_GET_DEVICE_TYPE(id) (uint8_t)((((uint32_t)(id)) >> CAN_ID_DEVICE_TYPE_SHIFT) & CAN_ID_DEVICE_TYPE_MASK)
#define CAN_ID_GET_NODE_ID(id)     (uint8_t)((((uint32_t)(id)) >> CAN_ID_NODE_ID_SHIFT)     & CAN_ID_NODE_ID_MASK)
#define CAN_ID_GET_MSG_TYPE(id)    (uint8_t)((((uint32_t)(id)) >> CAN_ID_MSG_TYPE_SHIFT)    & CAN_ID_MSG_TYPE_MASK)
#define CAN_ID_GET_SUB_TYPE(id)    (uint8_t)((((uint32_t)(id)) >> CAN_ID_SUB_TYPE_SHIFT)    & CAN_ID_SUB_TYPE_MASK)

#define CAN_PRIORITY_EMERGENCY     0U
#define CAN_PRIORITY_ALARM         1U
#define CAN_PRIORITY_CONTROL       2U
#define CAN_PRIORITY_DATA          3U
#define CAN_PRIORITY_STATUS        4U

#define CAN_DEVICE_GATEWAY         0x00U
#define CAN_DEVICE_COLLECTOR       0x01U

#define MSG_TYPE_SENSOR            0x01U
#define MSG_TYPE_NODE_STATUS       0x02U
#define MSG_TYPE_HEARTBEAT         0x03U
#define MSG_TYPE_OVER_TEMP         0x10U
#define MSG_TYPE_BUS_ERR           0x11U
#define MSG_TYPE_CTRL              0x20U
#define MSG_TYPE_CTRL_ACK          0x21U

#define SUB_TYPE_DEFAULT           0x00U
#define SUB_TYPE_SENSOR_ENV        0x00U
#define CAN_APP_FRAME_DLC          8U

/* Common V2.0 payload: data[0..1]=sequence, data[2..6]=body,
 * data[7]=CRC8(data[0..6]). */
typedef struct
{
 uint8_t priority;
 uint8_t device_type;
 uint8_t node_id;
 uint8_t msg_type;
 uint8_t sub_type;
 uint16_t seq;
 uint8_t payload[5];
 uint8_t crc8;
} CanParsedFrame_t;

/* CRC8F103 */
uint8_t app_calc_crc8(const uint8_t *buf, uint8_t len);

/* IDDLCCRC */
uint8_t app_can_validate_frame(const CanFrame_t *raw, CanParsedFrame_t *out);

/* CAN */
void TaskCanGateway(void *arg);
/* ???? */
extern void App_UpdateNodeTable(uint8_t node_id, uint16_t seq, const SensorData_t *sdata);

#endif

