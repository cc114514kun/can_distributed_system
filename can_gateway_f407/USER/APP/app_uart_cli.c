#include "app_uart_cli.h"
#include "bsp_uart.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "bsp_can.h"
#include "fault_mgr.h"
#include "app_can_gw.h"
#include "app_sys_mon.h"
#include "app_config.h"
#include "app_perf.h"
#include "app_sys_info.h"


QueueHandle_t g_report_queue = NULL;
static TaskHandle_t g_cli_task_handle = NULL;
static TaskHandle_t g_report_task_handle = NULL;

typedef struct
{
    char buf[REPORT_LINE_MAX_LEN];
}ReportMsg_t;


/* 异步打印接口，替代printf，各个业务任务调用；仅允许任务上下文调用，禁止ISR调用 */
BaseType_t App_ReportPrint(const char *fmt, ...)
{
    if(g_report_queue == NULL)
        return pdFALSE;

    ReportMsg_t msg;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg.buf, sizeof(msg.buf), fmt, ap);
    va_end(ap);
    msg.buf[REPORT_LINE_MAX_LEN - 1U] = '\0';  /* 防止截断无结束符 */

    /* Protocol responses (OK/ERR) and the CLI command-echo diagnostic are
     * critical for the host. Send them to the front of the queue so they are
     * emitted before any backlog of debug logs; use a longer wait because the
     * queue may be momentarily full. */
    if((msg.buf[0] == 'O') && (msg.buf[1] == 'K'))
    {
        return xQueueSendToFront(g_report_queue, &msg, pdMS_TO_TICKS(50));
    }
    if((msg.buf[0] == 'E') && (msg.buf[1] == 'R') && (msg.buf[2] == 'R'))
    {
        return xQueueSendToFront(g_report_queue, &msg, pdMS_TO_TICKS(50));
    }
    if((msg.buf[0] == '[') && (msg.buf[1] == 'C') &&
       (msg.buf[2] == 'L') && (msg.buf[3] == 'I'))
    {
        return xQueueSendToFront(g_report_queue, &msg, pdMS_TO_TICKS(50));
    }
    return xQueueSend(g_report_queue, &msg, pdMS_TO_TICKS(2));
}


/**
 * @brief ReportTask：专门做串口发送，解耦业务任务与UART硬件
 */
static void ReportTask(void *arg)
{
    (void)arg;
    ReportMsg_t msg;
    for(;;)
    {
        if(xQueueReceive(g_report_queue, &msg, pdMS_TO_TICKS(100U)) == pdPASS)
        {
            uint16_t len = strlen(msg.buf);
            bsp_uart_send((uint8_t*)msg.buf, len);
        }
        /* 上报心跳给系统监控 */
        AppSysMon_HeartBeat(TASK_IDX_REPORT);
    }
}

/**
 * @brief 简易故障码转字符串，cli使用（fault_mgr内部static函数不能外部调用）
 */
static const char* Cli_FaultCode2Str(FaultCode_t code)
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

/**
 * @brief CRC8 (poly 0x07, init 0) — same polynomial as the CAN sensor protocol.
 *        Used to verify integrity of SET_CONFIG payloads from the host.
 */
static uint8_t Cli_Crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0U;
    for(uint16_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for(uint8_t b = 0U; b < 8U; b++)
        {
            if(crc & 0x80U) crc = (uint8_t)((crc << 1U) ^ 0x07U);
            else            crc = (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

/**
 * @brief GET_CONFIG: report current SystemConfig_t from RAM (mirrors flash).
 */
static void Cli_GetConfig(void)
{
    App_ReportPrint("OK CONFIG TIMEOUT:%lu TEMP_HI:%d PERIOD:%lu NODEMASK:0x%08lX\r\n",
        (unsigned long)g_sys_cfg.node_timeout_ms,
        (int)g_sys_cfg.temp_high_limit,
        (unsigned long)g_sys_cfg.report_period_ms,
        (unsigned long)g_sys_cfg.node_enable_mask);
}

/**
 * @brief SET_CONFIG <timeout_ms> <temp_hi_q100> <period_ms> <node_mask_hex> [crc]
 *        Applies to g_sys_cfg, validates CRC8 if provided, then saves to flash.
 */
static void Cli_SetConfig(char *args)
{
    uint32_t timeout_ms, period_ms, mask;
    int32_t  temp_hi;
    int crc_arg = -1;
    int n = sscanf(args, "%lu %ld %lu %lx %x",
                   &timeout_ms, &temp_hi, &period_ms, &mask, &crc_arg);
    if(n < 4)
    {
        App_ReportPrint("ERR SET_CONFIG bad args\r\n");
        return;
    }
    /* Optional CRC8 over the 16-byte LE packed payload. */
    if(crc_arg >= 0)
    {
        uint8_t pay[16];
        memcpy(&pay[0],  &timeout_ms, 4);
        memcpy(&pay[4],  &temp_hi,   4);
        memcpy(&pay[8],  &period_ms, 4);
        memcpy(&pay[12], &mask,      4);
        if(Cli_Crc8(pay, sizeof(pay)) != (uint8_t)crc_arg)
        {
            App_ReportPrint("ERR SET_CONFIG CRC mismatch\r\n");
            return;
        }
    }
    /* Validate ranges. */
    if(timeout_ms < 50U || timeout_ms > 60000U ||
       period_ms  < 50U || period_ms  > 60000U ||
       temp_hi    < -4000 || temp_hi  > 20000)
    {
        App_ReportPrint("ERR SET_CONFIG out of range\r\n");
        return;
    }
    g_sys_cfg.node_timeout_ms  = timeout_ms;
    g_sys_cfg.temp_high_limit  = (int16_t)temp_hi;
    g_sys_cfg.report_period_ms = period_ms;
    g_sys_cfg.node_enable_mask = mask;
    if(AppConfig_SaveToFlash())
        App_ReportPrint("OK CONFIG_SAVED TIMEOUT:%lu TEMP_HI:%d PERIOD:%lu NODEMASK:0x%08lX\r\n",
            (unsigned long)timeout_ms, (int)temp_hi,
            (unsigned long)period_ms, (unsigned long)mask);
    else
        App_ReportPrint("ERR CONFIG save failed\r\n");
}

/**
 * @brief CLEAR_COUNTERS: reset CAN statistics and per-node counters/sequence.
 */
static void Cli_ClearCounters(void)
{
    memset(&g_can_stats, 0U, sizeof(g_can_stats));
    for(uint8_t i = 0U; i < MAX_NODE_NUM; i++)
    {
        nodes[i].rx_count      = 0U;
        nodes[i].lost_count    = 0U;
        nodes[i].crc_error_count = 0U;
        nodes[i].offline_count = 0U;
        nodes[i].recovery_count = 0U;
        nodes[i].last_seq      = 0U;
    }
    App_ReportPrint("OK COUNTERS_CLEARED\r\n");
}

/**
 * @brief RESET_DEVICE: acknowledge then trigger a software reset.
 */
static void Cli_ResetDevice(void)
{
    bsp_uart_send((uint8_t*)"OK REBOOTING\r\n", 14U);
    vTaskDelay(pdMS_TO_TICKS(200U));
    HAL_NVIC_SystemReset();
}

/**
 * @brief GET_PUSH: report current active-push config (push_enable / period).
 */
static void Cli_GetPush(void)
{
    App_ReportPrint("OK PUSH CFG ENABLE:%u PERIOD:%lu\r\n",
        (unsigned)g_sys_cfg.push_enable, (unsigned long)g_sys_cfg.push_period_ms);
}

/**
 * @brief SET_PUSH <enable> <period_ms>
 *        enable: 0=off 1=on; period_ms: 100..60000. Saves to flash.
 */
static void Cli_SetPush(char *args)
{
    uint32_t en, period;
    int n = sscanf(args, "%lu %lu", &en, &period);
    if(n < 2)
    {
        App_ReportPrint("ERR SET_PUSH bad args\r\n");
        return;
    }
    if(period < 100U || period > 60000U)
    {
        App_ReportPrint("ERR SET_PUSH period out of range\r\n");
        return;
    }
    g_sys_cfg.push_enable    = (uint8_t)(en ? 1U : 0U);
    g_sys_cfg.push_period_ms = period;
    if(AppConfig_SaveToFlash())
        App_ReportPrint("OK PUSH_SAVED ENABLE:%u PERIOD:%lu\r\n",
            (unsigned)g_sys_cfg.push_enable, (unsigned long)period);
    else
        App_ReportPrint("ERR PUSH save failed\r\n");
}

/**
 * @brief CLI命令处理，收到一行字符串执行对应命令
 */
static void cli_process_cmd(char *cmd_line)
{
    /* ---- optional trailing CRC8 frame check ----
     * The host appends "<space><2-hex CRC8>" to every command; the CRC is
     * computed over the command bytes (everything before that trailing space).
     * If present, verify it: a truncated/dropped byte makes the CRC mismatch,
     * in which case we reject with ERR BAD_FRAME instead of an ambiguous
     * "Unknown cmd". The host retries up to MAX_RETRY on BAD_FRAME/Unknown cmd,
     * so a single transient RX drop is transparent to the user.
     * A frame without a trailing CRC token is still accepted (backward compat
     * for manual terminal typing). */
    char *sp = strrchr(cmd_line, ' ');
    if(sp != NULL)
    {
        char *tok = sp + 1;
        if((strlen(tok) == 2U) &&
           isxdigit((unsigned char)tok[0]) && isxdigit((unsigned char)tok[1]))
        {
            uint8_t rx_crc = (uint8_t)strtol(tok, NULL, 16);
            *sp = '\0';  /* cut command part for CRC computation */
            uint8_t calc = Cli_Crc8((const uint8_t*)cmd_line,
                                    (uint16_t)strlen(cmd_line));
            if(calc != rx_crc)
            {
                App_ReportPrint("ERR BAD_FRAME\r\n");
                return;
            }
        }
    }

    if(strcmp(cmd_line,"GET_STATUS") == 0)
    {
        App_ReportPrint("OK CAN Stat:Invalid:%u BusOffRetry:%u\r\n",
            g_can_stats.invalid_frame_count, g_can_stats.busoff_retry_cnt);
    }
    else if(strcmp(cmd_line,"GET_FAULT") == 0)
    {
        FaultEvent_t evt_buf[4];
        uint16_t cnt = FaultMgr_GetHistory(evt_buf,4);
        App_ReportPrint("OK FAULT_CNT:%u\r\n", cnt);
        for(uint16_t i=0;i<cnt;i++)
        {
            App_ReportPrint("  TS:%lu CODE:%s NODE:%d PARAM:%lu\r\n",
                evt_buf[i].timestamp,
                Cli_FaultCode2Str(evt_buf[i].fault_code),
                evt_buf[i].node_id, evt_buf[i].param);
        }
    }
    else if(strcmp(cmd_line,"CLEAR_FAULT") == 0)
    {
        FaultMgr_ClearHistory();
        App_ReportPrint("OK FAULT_CLEARED\r\n");
    }
    else if(strcmp(cmd_line,"GET_VERSION") == 0)
    {
        App_ReportPrint("OK\r\n");
        App_ReportPrint("Product: %s\r\n", PRODUCT_NAME);
        App_ReportPrint("HW: %s\r\n", HW_VER_STR);
        App_ReportPrint("FW: %s\r\n", FW_VER_STR);
        App_ReportPrint("Protocol: %s\r\n", PROTOCOL_VER_STR);
        App_ReportPrint("Build: %s\r\n", BUILD_DATE_STR);
    }
    else if(strcmp(cmd_line,"GET_NODE") == 0)
    {
        /* 遍历节点表nodes[] */
        App_ReportPrint("OK NODE_LIST\r\n");
        for(uint8_t i=0;i<MAX_NODE_NUM;i++)
        {
            SensorNode_t *pn = &nodes[i];
            App_ReportPrint("  ID:%u Seq:%u Online:%u\r\n",
                i+1U, pn->last_seq, pn->online);
        }
    }
    else if(strcmp(cmd_line,"GET_SENSOR") == 0)
    {
        App_ReportPrint("OK SENSOR_DATA\r\n");
        for(uint8_t i=0;i<MAX_NODE_NUM;i++)
        {
            SensorNode_t *pn = &nodes[i];
            uint8_t k1 = (pn->data.key_state >> 0U) & 0x01U;
            uint8_t k2 = (pn->data.key_state >> 1U) & 0x01U;
            App_ReportPrint("  N%u VoltQ100:%u Temp:%d K1:%u K2:%u\r\n",
                i+1U, pn->data.adc0, (int16_t)pn->data.adc1, k1, k2);
        }
    }
    else if(strcmp(cmd_line,"GET_SYS") == 0)
    {
        App_ReportPrint("OK SYS_INFO\r\n");
        App_ReportPrint("  UPTIME:%lu s\r\n", (unsigned long)AppSysInfo_GetUptimeSec());
        App_ReportPrint("  BOOT_COUNT:%lu\r\n", (unsigned long)AppSysInfo_GetBootCount());
        App_ReportPrint("  RESET_REASON:%s\r\n", AppSysInfo_GetReasonStr());
        if(CanRxQueue != NULL)
        {
            UBaseType_t used = uxQueueMessagesWaiting(CanRxQueue);
            App_ReportPrint("  CAN_QUEUE used:%u / %u\r\n",used,CAN_RX_QUEUE_LEN);
        }
    }
    else if(strcmp(cmd_line,"GET_CONFIG") == 0)
    {
        Cli_GetConfig();
    }
    else if(strncmp(cmd_line,"SET_CONFIG ", 11U) == 0)
    {
        Cli_SetConfig(cmd_line + 11U);
    }
    else if(strcmp(cmd_line,"CLEAR_COUNTERS") == 0)
    {
        Cli_ClearCounters();
    }
    else if(strcmp(cmd_line,"RESET_DEVICE") == 0)
    {
        Cli_ResetDevice();
    }
    else if(strcmp(cmd_line,"GET_PERF") == 0)
    {
        AppPerf_Report();
    }
    else if(strcmp(cmd_line,"GET_PUSH") == 0)
    {
        Cli_GetPush();
    }
    else if(strncmp(cmd_line,"SET_PUSH ", 9U) == 0)
    {
        Cli_SetPush(cmd_line + 9U);
    }
    else
    {
        App_ReportPrint("ERR Unknown cmd: %s\r\n", cmd_line);
    }
}


static void CliTask(void *arg)
{
    (void)arg;
    uint8_t line_buf[128];
    uint16_t line_len;

    for(;;)
    {
        AppPerf_Update();   /* self-throttled 1 Hz */
        if(bsp_uart_read_line(line_buf, sizeof(line_buf), &line_len))
        {
            cli_process_cmd((char*)line_buf);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Active push task (P2). When g_sys_cfg.push_enable != 0, periodically
 *        emits PUSH lines (status + per-node) via App_ReportPrint so the host
 *        gets real-time data without polling. PUSH lines are self-generated
 *        (not responses) and carry the "PUSH " prefix so the host can separate
 *        them from command responses. Task priority is lowest (1) to avoid
 *        disturbing real-time CAN/CLI tasks; sending is still async through
 *        the report queue + ReportTask, preserving the UART-silent principle.
 */
static void App_PushTask(void *arg)
{
    (void)arg;
    for(;;)
    {
        uint32_t period = g_sys_cfg.push_period_ms;
        if(period < 100U) period = 100U;

        if(g_sys_cfg.push_enable != 0U)
        {
            App_ReportPrint("PUSH STATUS can_invalid=%lu busoff_retry=%u uptime=%lus\r\n",
                (unsigned long)g_can_stats.invalid_frame_count,
                (unsigned)g_can_stats.busoff_retry_cnt,
                (unsigned long)AppSysInfo_GetUptimeSec());

            for(uint8_t i = 0U; i < MAX_NODE_NUM; i++)
            {
                /* Skip nodes disabled by node_enable_mask. */
                if((g_sys_cfg.node_enable_mask & (1U << i)) == 0U)
                {
                    continue;
                }
                SensorNode_t *pn = &nodes[i];
                uint8_t k1 = (uint8_t)((pn->data.key_state >> 0U) & 0x01U);
                uint8_t k2 = (uint8_t)((pn->data.key_state >> 1U) & 0x01U);
                App_ReportPrint("PUSH NODE n=%u on=%u volt=%u temp=%d k1=%u k2=%u seq=%u\r\n",
                    (unsigned)(i + 1U),
                    pn->online ? 1U : 0U,
                    (unsigned)pn->data.adc0,
                    (int16_t)pn->data.adc1,
                    (unsigned)k1, (unsigned)k2,
                    (unsigned)pn->last_seq);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(period));
    }
}


void AppCli_Init(UART_HandleTypeDef *huart)
{
    g_report_queue = xQueueCreate(REPORT_QUEUE_LEN, sizeof(ReportMsg_t));
    if(g_report_queue != NULL)
    {
        bsp_uart_init(huart);
        /* ReportTask must outrank every business task (App_MainTask=5,
         * TaskCanGateway=4, node/fault=3) so the UART send queue is drained
         * promptly; otherwise a burst of prints gets starved and only partial
         * bytes reach the wire. */
        xTaskCreate(ReportTask, "ReportTask", 512, NULL, 6, &g_report_task_handle);
        /* CliTask handles host commands. Keep it just below ReportTask so
         * incoming command lines are parsed promptly even when CAN/SysMon
         * tasks are busy printing. */
        xTaskCreate(CliTask, "CliTask", 768, NULL, 5, &g_cli_task_handle);
        /* Active push task (lowest priority; sends async via report queue). */
        xTaskCreate(App_PushTask, "PushTask", 512, NULL, 1, NULL);
    }
}

