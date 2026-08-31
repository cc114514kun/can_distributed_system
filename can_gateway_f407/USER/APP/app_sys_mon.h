#ifndef __APP_SYS_MON_H
#define __APP_SYS_MON_H

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* 被监控任务索引，和hb_table一一对应 */
typedef enum
{
    TASK_IDX_CAN_GW      = 0U,
    TASK_IDX_REPORT      = 1U,
    TASK_IDX_NODE_MON    = 2U,
    TASK_IDX_FAULT_MGR   = 3U,
    TASK_IDX_MAX
}SysMonTaskIndex_t;

/**
 * @brief 业务任务每轮循环调用，上报心跳
 * @param task_idx 任务编号 SysMonTaskIndex_t
 */
void AppSysMon_HeartBeat(SysMonTaskIndex_t task_idx);

/**
 * @brief 初始化系统监控任务，在app_main调用
 */
void AppSysMon_Init(void);

#endif

