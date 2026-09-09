#ifndef __BSP_CAN_H
#define __BSP_CAN_H
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>
#include <string.h>
#include "task.h"
#include <stdbool.h>

#define CAN_RX_QUEUE_LEN 32U
#define MAX_NODE_NUM 4U
#define NODE_DEBUG_PRINT 0U
#define CAN_BUSOFF_MAX_RETRY    3U   //最大恢复尝试次数

/* F3.4 Ring buffer: per‑node historical samples. */
#define RING_BUF_SAMPLE_CNT 16U
/* F3.2 Threshold check. Temperature high-limit is taken from g_sys_cfg
   (temp_high_limit, Q100); voltage bounds are physical sanity limits. */
#define VOLT_MIN 0.0f
#define VOLT_MAX 3.3f

/* F3.1 Decoded physical quantities. */
typedef struct
{
 float temperature;
 float voltage;
} PhysData_t;

/* F3.4 Ring buffer. */
typedef struct
{
 PhysData_t buf[RING_BUF_SAMPLE_CNT];
 uint8_t wr_idx;
 uint8_t rd_idx;
 uint8_t sample_cnt;
 uint32_t overwrite_cnt;
} SensorRingBuf_t;

/* F1.2 CAN frame struct. */
typedef struct
{
 uint32_t id; /* CAN ID (ExtId for ext, StdId for std). */
 uint8_t dlc; /* Data length code. */
 uint8_t ide; /* 1=29bit ext, 0=11bit std, used for diagnostics. */
 uint8_t data[8]; /* Data payload, up to 8 bytes. */
 uint32_t timestamp; /* Timestamp, SysTick count. */
} CanFrame_t;

/* F1.3 Statistics counters.
 * 新增 busoff_retry_cnt 用于Bus‑Off重试计数 */
typedef struct
{
 uint8_t  busoff_retry_cnt;        // Bus‑Off恢复重试计数
 uint32_t rx_count;                // RX count.
 uint32_t tx_count;                // TX count.
 uint32_t queue_overflow_count;    // Queue overflow count.
 uint32_t invalid_frame_count;    // Invalid frame count.
 uint32_t busoff_recovery_count;   // Bus‑Off recovery count.
 uint32_t send_fail_count;         // Send fail count.
} CanStats_t;

/* Sensor data reported by slave nodes, matches F103 frame layout. */
typedef struct
{
 uint16_t adc0;
 uint16_t adc1;
 uint16_t adc2;
 uint8_t key_state;
 uint8_t fault_code;
} SensorData_t;

/* F3.3 Per‑node state table. */
typedef struct
{
 uint8_t node_id;
 bool online;
 bool alarm; /* F3.2 alarm state. */
 uint32_t rx_count;
 uint32_t lost_count;
 uint32_t crc_error_count;
 uint32_t offline_count;
 uint32_t recovery_count;
 TickType_t last_rx_tick;
 uint16_t last_seq;
 bool seq_valid;
 SensorData_t data; /* F1 raw data: ADC, key_state. */
 PhysData_t phys_data; /* F3.1 decoded physical quantities. */
 SensorRingBuf_t ring_buf; /* F3.4 per‑node history ring buffer. */
} SensorNode_t;

extern QueueHandle_t CanRxQueue;
extern CanStats_t g_can_stats;
extern volatile uint8_t g_can_busoff_flag;
extern SensorNode_t nodes[MAX_NODE_NUM];

HAL_StatusTypeDef bsp_can_init(void);
HAL_StatusTypeDef bsp_can_busoff_recovery(void);
/* Generic send: id>0x7FF -> 29bit ext, else 11bit std; len<=8. */
HAL_StatusTypeDef bsp_can_send(uint32_t id, const uint8_t *data, uint8_t len);
/* Internal loopback self‑test: validates F407 hardware / IRQ / queue / parsing
 * without needing F103 or wiring. Returns HAL_OK on success. */
HAL_StatusTypeDef bsp_can_loopback_test(uint32_t id, const uint8_t *data, uint8_t len);
/* bxCAN hardware self‑test: print MSR / ESR / BTR.
 * MSR.INA must be 0 (in NORMAL); ESR should be 0 (no error);
 * BTR should reflect current baud‑rate config. */
void bsp_can_self_diag(void);
void App_NodeTable_Init(void);
void App_NodeMonitorTask(void *pvParameters);
void App_UpdateNodeTable(uint8_t node_id, uint16_t seq, const SensorData_t *p_sensor);


#endif

