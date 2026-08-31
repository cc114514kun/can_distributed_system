#ifndef __APP_CAN_GW_H
#define __APP_CAN_GW_H

#include "bsp_can.h"
#include <stdint.h>

/* F103CANID 29bitID
ID [NodeID(8bit)][MsgType(8bit)][Sequence(16bit)]
*/
#define CAN_ID_GET_NODEID(id) (((id) >> 16U) & 0xFFU)
#define CAN_ID_GET_MSGTYPE(id) (((id) >> 8U) & 0xFFU)
#define CAN_ID_GET_SEQ(id) ((id) & 0xFFFFU)

/* Payload[0~3] 4data[4]=CRC8 */
typedef struct
{
 uint8_t node_id; // ID
 uint8_t msg_type; // 
 uint16_t seq; // 
 uint8_t payload[4]; // 
 uint8_t crc8; // CRC8
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

