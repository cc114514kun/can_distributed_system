#ifndef __FAULT_MGR_H
#define __FAULT_MGR_H

#include "FreeRTOS.h"
#include <stdint.h>

#define FAULT_QUEUE_LEN     8U
#define FAULT_HISTORY_CNT   16U

/* */
typedef enum
{
 FAULT_NONE = 0,
 FAULT_CAN_BUS_OFF,
 FAULT_CAN_RX_OVERFLOW,
 FAULT_CAN_PROTOCOL,
 FAULT_CAN_ILLEGAL_FRAME,
 FAULT_NODE_OFFLINE,
 FAULT_SENSOR_CRC,
 FAULT_SENSOR_SEQUENCE,
 FAULT_SENSOR_ALARM,
 FAULT_NODE_CAN_ERROR,
 FAULT_UART_OVERFLOW,
 FAULT_CONFIG_CRC,
 FAULT_SYSTEM_FATAL,
}FaultCode_t;

/* */
typedef struct
{
 uint32_t timestamp; // tick
 FaultCode_t fault_code; // 
 uint8_t node_id; // 0xFF
 uint32_t param; // /
} FaultEvent_t;

/* */
void FaultMgr_Init(void);

/**
 * @brief 
 * @param code 
 * @param node_id ID0xFF
 * @param param 
 * @return pdTRUE
 */
BaseType_t FaultMgr_PostEvent(FaultCode_t code, uint8_t node_id, uint32_t param);

/* buf */
uint16_t FaultMgr_GetHistory(FaultEvent_t *buf, uint16_t buf_len);
void FaultMgr_ClearHistory(void);

#endif

