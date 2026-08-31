#include "fault_mgr.h"
#include "main.h"
#include "task.h"
#include "bsp_can.h"
#include <string.h>
#include "app_sys_mon.h"
#include "app_uart_cli.h"

#define FAULT_MGR_DEBUG_PRINT     0U

QueueHandle_t g_fault_queue = NULL;
static FaultEvent_t g_fault_history[FAULT_HISTORY_CNT];
static uint16_t g_hist_wr_idx = 0U;
static uint16_t g_hist_count = 0U;
static TaskHandle_t FaultMgrTask_Handle = NULL;

/**
 * @brief 故障码转字符串，只做转换，禁止执行硬件操作
 */
static const char* FaultMgr_Code2Str(FaultCode_t code)
{
    switch(code)
    {
        case FAULT_NONE:                return "FAULT_NONE";
        case FAULT_CAN_BUS_OFF:         return "FAULT_CAN_BUS_OFF";
        case FAULT_CAN_RX_OVERFLOW:     return "FAULT_CAN_RX_OVERFLOW";
        case FAULT_CAN_PROTOCOL:        return "FAULT_CAN_PROTOCOL";
        case FAULT_CAN_ILLEGAL_FRAME:   return "FAULT_CAN_ILLEGAL_FRAME";
        case FAULT_NODE_OFFLINE:        return "FAULT_NODE_OFFLINE";
        case FAULT_SENSOR_CRC:          return "FAULT_SENSOR_CRC";
        case FAULT_SENSOR_SEQUENCE:     return "FAULT_SENSOR_SEQUENCE";
        case FAULT_UART_OVERFLOW:       return "FAULT_UART_OVERFLOW";
        case FAULT_CONFIG_CRC:          return "FAULT_CONFIG_CRC";
        case FAULT_SYSTEM_FATAL:        return "FAULT_SYSTEM_FATAL";
        default:                        return "FAULT_UNKNOWN";
    }
}

static void FaultMgr_SaveHistory(const FaultEvent_t *evt)
{
    if(evt == NULL)
        return;
    g_fault_history[g_hist_wr_idx] = *evt;
    g_hist_wr_idx = (g_hist_wr_idx + 1U) % FAULT_HISTORY_CNT;
    if(g_hist_count < FAULT_HISTORY_CNT)
    {
        g_hist_count++;
    }
}

/**
 * @brief 故障恢复动作：FAULT_CAN_BUS_OFF恢复交给TaskCanGateway处理，此处只处理致命复位
 */
static void FaultMgr_DoRecovery(FaultCode_t code)
{
    switch(code)
    {
        case FAULT_CAN_ILLEGAL_FRAME:
        case FAULT_SENSOR_CRC:
        case FAULT_NODE_OFFLINE:
        case FAULT_SENSOR_SEQUENCE:
        case FAULT_CAN_RX_OVERFLOW:
        case FAULT_CAN_PROTOCOL:
        case FAULT_UART_OVERFLOW:
        case FAULT_CONFIG_CRC:
        case FAULT_CAN_BUS_OFF:
            /* CAN Bus‑Off恢复逻辑迁移到TaskCanGateway，此处只记录事件，不重复执行恢复 */
            break;

        case FAULT_SYSTEM_FATAL:
        default:
            NVIC_SystemReset();
            break;
    }
}

static void FaultMgrTask(void *pvParam)
{
    (void)pvParam;
    FaultEvent_t evt;
    for(;;)
    {
        if(xQueueReceive(g_fault_queue, &evt, pdMS_TO_TICKS(100U)) == pdPASS)
        {
            FaultMgr_SaveHistory(&evt);

#if FAULT_MGR_DEBUG_PRINT
            App_ReportPrint("[FAULT] TS:%lu CODE:%s NODE:%d PARAM:%lu\r\n",
                evt.timestamp,
                FaultMgr_Code2Str(evt.fault_code),
                evt.node_id,
                evt.param);
#endif

            // HAL_GPIO_WritePin(ERR_LED_GPIO_Port, ERR_LED_Pin, GPIO_PIN_SET);

            FaultMgr_DoRecovery(evt.fault_code);
        }
        /* 上报系统监控心跳 */
        AppSysMon_HeartBeat(TASK_IDX_FAULT_MGR);
    }
}

void FaultMgr_Init(void)
{
    g_fault_queue = xQueueCreate(FAULT_QUEUE_LEN, sizeof(FaultEvent_t));
    if(g_fault_queue != NULL)
    {
#if FAULT_MGR_DEBUG_PRINT
        App_ReportPrint("[FAULT_MGR] queue create ok\r\n");
#endif
        xTaskCreate(FaultMgrTask,
        "FaultMgrTask",
        768,
        NULL,
        3,
        &FaultMgrTask_Handle);
    }
    else
    {
#if FAULT_MGR_DEBUG_PRINT
        App_ReportPrint("[FAULT_MGR] queue create FAULT\r\n");
#endif
    }
}

BaseType_t FaultMgr_PostEvent(FaultCode_t code, uint8_t node_id, uint32_t param)
{
    if(g_fault_queue == NULL)
    {
        return pdFALSE;
    }
    FaultEvent_t evt;
    evt.timestamp = xTaskGetTickCount();
    evt.fault_code = code;
    evt.node_id = node_id;
    evt.param = param;

    if(xPortIsInsideInterrupt() != pdFALSE)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        BaseType_t ret = xQueueSendFromISR(g_fault_queue, &evt, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return ret;
    }
    else
    {
        return xQueueSend(g_fault_queue, &evt, pdMS_TO_TICKS(10U));
    }
}

uint16_t FaultMgr_GetHistory(FaultEvent_t *buf, uint16_t buf_len)
{
    uint16_t copy_cnt = 0U;
    if(buf == NULL || buf_len == 0U)
        return 0U;

    uint16_t max_copy = (buf_len < g_hist_count) ? buf_len : g_hist_count;
    for(uint16_t i = 0U; i < max_copy; i++)
    {
        uint16_t idx = (g_hist_wr_idx + FAULT_HISTORY_CNT - 1U - i) % FAULT_HISTORY_CNT;
        buf[copy_cnt] = g_fault_history[idx];
        copy_cnt++;
    }
    return copy_cnt;
}

void FaultMgr_ClearHistory(void)
{
    memset(g_fault_history, 0, sizeof(g_fault_history));
    g_hist_wr_idx = 0U;
    g_hist_count = 0U;
}

