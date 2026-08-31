#include "app_sys_mon.h"
#include "bsp_iwdg.h"
#include "app_uart_cli.h"
#include "bsp_can.h"
#include "fault_mgr.h"
#include "bsp_gpio.h"   /* LED_CAN_PIN for independent liveness heartbeat */

/* 调试开关：0关闭看门狗(方便断点调试) 1开启独立看门狗 */
#define SYS_MON_ENABLE_IWDG     1U

/* 任务心跳记录表 */
typedef struct
{
    TaskHandle_t    hdl;
    volatile uint32_t beat_cnt;     //任务自增心跳计数
    uint32_t        beat_last;      //上一次读到的计数值
    uint32_t        timeout_ms;     //心跳超时时间 ms
    uint8_t         stall_cnt;      //新增：连续无心跳计数
}TaskHeartbeat_t;

static TaskHeartbeat_t hb_list[TASK_IDX_MAX] =
{
    {NULL, 0, 0, 300, 0},  // CAN_GW
    {NULL, 0, 0, 300, 0},  // REPORT
    {NULL, 0, 0, 500, 0},  // NODE_MON
    {NULL, 0, 0, 300, 0},  // FAULT_MGR
};

static TaskHandle_t g_sys_mon_handle = NULL;

/**
 * @brief 业务任务调用：刷新心跳计数
 */
void AppSysMon_HeartBeat(SysMonTaskIndex_t task_idx)
{
    if(task_idx < TASK_IDX_MAX)
    {
        hb_list[task_idx].beat_cnt++;
    }
}

static void AppSysMonTask(void *arg)
{
    (void)arg;

    /* Immediate, queue-independent LED blink: if this runs, LED_CAN toggles
     * three times.  If the LED never blinks, the task was never scheduled or
     * xTaskCreate failed. */
    for(uint8_t i = 0U; i < 6U; i++)
    {
        BSP_Led_Toggle(LED_CAN_PIN);
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    /* 循环重试获取全部任务句柄，等待所有业务任务创建完成 */
    for(uint32_t retry = 0U; retry < 20U; retry++)
    {
        hb_list[TASK_IDX_CAN_GW].hdl       = xTaskGetHandle("CanGw");
        hb_list[TASK_IDX_REPORT].hdl       = xTaskGetHandle("ReportTask");
        hb_list[TASK_IDX_NODE_MON].hdl     = xTaskGetHandle("App_NodeMonitorTask");
        hb_list[TASK_IDX_FAULT_MGR].hdl    = xTaskGetHandle("FaultMgrTask");

        if( hb_list[TASK_IDX_CAN_GW].hdl != NULL &&
            hb_list[TASK_IDX_REPORT].hdl != NULL &&
            hb_list[TASK_IDX_NODE_MON].hdl != NULL &&
            hb_list[TASK_IDX_FAULT_MGR].hdl != NULL )
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }

#if SYS_MON_ENABLE_IWDG
    /* 所有业务任务句柄已就绪再启动看门狗，确保第一次喂狗只需再等一个
       监控周期(200ms)，避免 LSI 频率漂移导致启动期误复位。超时约1000ms。 */
    BSP_IWDG_Start();
#endif

    vTaskDelay(pdMS_TO_TICKS(100U));

    uint32_t mon_cycle = 0U;
    App_ReportPrint("[SYS_MON] monitor start: CanGw=%d Report=%d NodeMon=%d FaultMgr=%d\r\n",
                    TASK_IDX_CAN_GW, TASK_IDX_REPORT, TASK_IDX_NODE_MON, TASK_IDX_FAULT_MGR);

    for(;;)
    {
        /* Independent liveness heartbeat: toggling LED_CAN every monitor cycle
         * proves the scheduler (and SysMon) is still running. If this LED stops
         * blinking AND the serial also dies, it is a FULL system lockup. If the
         * LED keeps blinking but no serial appears, ReportTask (the only UART
         * consumer) is dead -> the print link itself is blocked. */
        BSP_Led_Toggle(LED_CAN_PIN);

        uint8_t is_system_healthy = 1U;
        uint8_t stall_flag = 0U;
        uint8_t beat_delta[4] = {0};

        /* ========= F7.1 任务心跳检测 ========= */
        for(uint8_t i = 0; i < TASK_IDX_MAX; i++)
        {
            TaskHeartbeat_t *p = &hb_list[i];

            if(p->hdl == NULL)
            {
                is_system_healthy = 0U;
                stall_flag = 1U;
                App_ReportPrint("[SYS_MON] TASK[%d] HANDLE NULL!\r\n", i);
                continue;
            }

            uint32_t curr_beat = p->beat_cnt;
            uint32_t delta = curr_beat - p->beat_last;   /* beats since last check */
            p->beat_last = curr_beat;

            if(delta == 0U)
            {
                p->stall_cnt++;
                /* 连续3次监控周期心跳不变，判定任务卡死 */
                if(p->stall_cnt >= 3U)
                {
                    is_system_healthy = 0U;
                    stall_flag = 1U;
                    App_ReportPrint("[SYS_MON] TASK[%d] HEARTBEAT STALL! (no beat in 3 checks)\r\n", i);
                }
            }
            else
            {
                if(p->stall_cnt >= 3U)
                {
                    App_ReportPrint("[SYS_MON] TASK[%d] HEARTBEAT RECOVERY\r\n", i);
                }
                p->stall_cnt = 0U;
            }

            beat_delta[i] = (uint8_t)delta;

            /* ========= F7.3 栈高水位，仅每 60s (300 cycles) 打印一次 ========= */
            if((mon_cycle % 300U) == 0U)
            {
                uint16_t stack_free = uxTaskGetStackHighWaterMark(p->hdl);
                App_ReportPrint("[SYS_MON] TASK%d stack_high_water:%u\r\n", i, stack_free);
            }
        }

        /* ========= F7.2 队列监控 使用率 ========= */
        UBaseType_t can_q_used = 0U;
        if(CanRxQueue != NULL)
        {
            can_q_used = uxQueueMessagesWaiting(CanRxQueue);
        }

        /* Only print summary when a stall/recovery happened. Normal operation
         * is silent so host CLI commands do not collide with spontaneous logs. */
        if(stall_flag != 0U)
        {
            App_ReportPrint("[SYS_MON] beats=%u,%u,%u,%u queue=%u stall=%u\r\n",
                            beat_delta[0], beat_delta[1], beat_delta[2], beat_delta[3],
                            (unsigned)can_q_used, stall_flag);
        }

        /* ========= F7.4 看门狗逻辑：只兜底整机冻结 =========
           只要 SysMonTask 能跑到这里（调度器没死锁）就喂狗；单任务停跳仅打
           日志告警，不再停止喂狗，避免 CAN 总线空闲时 CAN_GW 心跳为零被误判
           为系统故障而误复位。 */
#if SYS_MON_ENABLE_IWDG
        BSP_IWDG_Feed();
        if(is_system_healthy == 0U)
        {
            App_ReportPrint("[SYS_MON] task stalled but IWDG fed (idle-bus tolerant)\r\n");
        }
#endif

        mon_cycle++;
        vTaskDelay(pdMS_TO_TICKS(200U));
    }
}

void AppSysMon_Init(void)
{
    BaseType_t ret;
    ret = xTaskCreate(AppSysMonTask, "SysMon", 1536, NULL, 4, &g_sys_mon_handle);
    if(ret != pdPASS)
    {
        App_ReportPrint("[APP] SysMonTask create FAILED (heap=%lu)\r\n",
                        (unsigned long)xPortGetFreeHeapSize());
    }
}

