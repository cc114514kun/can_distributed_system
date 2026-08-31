#include "app_perf.h"
#include "app_uart_cli.h"
#include "bsp_can.h"
#include "fault_mgr.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

/* Queue handles used for peak-usage sampling.  They are defined in their
   respective .c files; declared here (where queue.h is available) so the
   type is complete when calling uxQueueMessagesWaiting(). */
extern QueueHandle_t g_fault_queue;
extern QueueHandle_t g_report_queue;

/* ---- runtime rate accumulators ---- */
static uint32_t g_uart_tx_bytes_acc = 0U;

/* ---- 1 Hz sampled values ---- */
static uint32_t s_prev_can_rx = 0U;
static uint32_t s_prev_can_tx = 0U;
static uint32_t s_prev_uart   = 0U;
static uint32_t s_rate_can_rx = 0U;
static uint32_t s_rate_can_tx = 0U;
static uint32_t s_rate_uart   = 0U;

static uint8_t  s_peak_canrx  = 0U;
static uint8_t  s_peak_fault  = 0U;
static uint8_t  s_peak_report = 0U;
static uint32_t s_peak_heap_used = 0U;

static TickType_t s_last_tick = 0U;

void AppPerf_UartTxBytes(uint16_t n)
{
    g_uart_tx_bytes_acc += (uint32_t)n;
}

void AppPerf_Update(void)
{
    TickType_t now = xTaskGetTickCount();
    if((now - s_last_tick) < pdMS_TO_TICKS(1000U))
    {
        return;
    }
    s_last_tick = now;

    s_rate_can_rx = g_can_stats.rx_count - s_prev_can_rx;
    s_rate_can_tx = g_can_stats.tx_count - s_prev_can_tx;
    s_rate_uart   = g_uart_tx_bytes_acc - s_prev_uart;
    s_prev_can_rx = g_can_stats.rx_count;
    s_prev_can_tx = g_can_stats.tx_count;
    s_prev_uart   = g_uart_tx_bytes_acc;

    if(CanRxQueue != NULL)
    {
        UBaseType_t u = uxQueueMessagesWaiting(CanRxQueue);
        if((uint8_t)u > s_peak_canrx) s_peak_canrx = (uint8_t)u;
    }
    if(g_fault_queue != NULL)
    {
        UBaseType_t u = uxQueueMessagesWaiting(g_fault_queue);
        if((uint8_t)u > s_peak_fault) s_peak_fault = (uint8_t)u;
    }
    if(g_report_queue != NULL)
    {
        UBaseType_t u = uxQueueMessagesWaiting(g_report_queue);
        if((uint8_t)u > s_peak_report) s_peak_report = (uint8_t)u;
    }

    uint32_t min_free = xPortGetMinimumEverFreeHeapSize();
    uint32_t used = (uint32_t)configTOTAL_HEAP_SIZE - min_free;
    if(used > s_peak_heap_used) s_peak_heap_used = used;
}

void AppPerf_Report(void)
{
    char buf[96];
    App_ReportPrint("OK PERF\r\n");

    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = (TaskStatus_t *)pvPortMalloc(n * sizeof(TaskStatus_t));
    if(tasks != NULL)
    {
        uint32_t total_rt = 0U;
        n = uxTaskGetSystemState(tasks, n, &total_rt);
        for(UBaseType_t i = 0U; i < n; i++)
        {
            uint32_t pct = total_rt ? (tasks[i].ulRunTimeCounter * 100UL) / total_rt : 0UL;
            uint32_t hwm_bytes = uxTaskGetStackHighWaterMark(tasks[i].xHandle) * 4U;
            (void)snprintf(buf, sizeof(buf),
                "  %-14s P:%2lu CPU:%2lu%% HWM:%5luB",
                tasks[i].pcTaskName,
                (unsigned long)tasks[i].uxCurrentPriority,
                (unsigned long)pct,
                (unsigned long)hwm_bytes);
            App_ReportPrint("%s\r\n", buf);
        }
        vPortFree(tasks);
    }

    (void)snprintf(buf, sizeof(buf),
        "  QPEAK canrx=%u fault=%u report=%u",
        s_peak_canrx, s_peak_fault, s_peak_report);
    App_ReportPrint("%s\r\n", buf);

    (void)snprintf(buf, sizeof(buf),
        "  RATE canrx=%u/s cantx=%u/s uarttx=%uB/s",
        s_rate_can_rx, s_rate_can_tx, s_rate_uart);
    App_ReportPrint("%s\r\n", buf);

    (void)snprintf(buf, sizeof(buf),
        "  HEAP_PEAK_USED=%uB TOTAL=%uB",
        s_peak_heap_used, (uint32_t)configTOTAL_HEAP_SIZE);
    App_ReportPrint("%s\r\n", buf);
}
