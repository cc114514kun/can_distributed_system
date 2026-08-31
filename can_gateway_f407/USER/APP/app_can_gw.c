#include "app_can_gw.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_can.h"
#include "fault_mgr.h"
#include "app_sys_mon.h"
#include "app_uart_cli.h"
#include "app_config.h" /* g_sys_cfg.node_enable_mask */

/* CRC8 polynomial 0x07 for F103 node frame */
uint8_t app_calc_crc8(const uint8_t *buf, uint8_t len)
{
    uint8_t crc = 0U;
    for(uint8_t i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for(uint8_t bit = 0; bit < 8; bit++)
        {
            if(crc & 0x80U)
            {
                crc = (crc << 1) ^ 0x07U;
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief 校验原始CAN帧，解析出node_id、msg_type、seq、payload、crc8
 * @retval 1:校验通过 0:校验失败
 */
uint8_t app_can_validate_frame(const CanFrame_t *raw, CanParsedFrame_t *out)
{
    if(raw == NULL || out == NULL)
        return 0;

    /* F103节点固定DLC=5 */
    if(raw->dlc != 5U)
    {
        g_can_stats.invalid_frame_count++;
        FaultMgr_PostEvent(FAULT_CAN_ILLEGAL_FRAME, 0xFF, raw->dlc);
        return 0;
    }

    /* 从29bit扩展ID解析各个域 */
    out->node_id = CAN_ID_GET_NODEID(raw->id);
    out->msg_type = CAN_ID_GET_MSGTYPE(raw->id);
    out->seq     = CAN_ID_GET_SEQ(raw->id);

    /* 节点ID合法性校验 */
    if(out->node_id == 0 || out->node_id > MAX_NODE_NUM)
    {
        g_can_stats.invalid_frame_count++;
        FaultMgr_PostEvent(FAULT_CAN_ILLEGAL_FRAME, out->node_id, 0U);
        return 0;
    }

    /* 拷贝4字节有效payload，第5字节为CRC8 */
    int i;
    for(i = 0; i < 4; i++)
    {
        out->payload[i] = raw->data[i];
    }
    out->crc8 = raw->data[4];

    return 1;
}

void TaskCanGateway(void *arg)
{
    (void)arg;
    CanFrame_t raw_frame;
    CanParsedFrame_t parsed;
    for(;;)
    {
        if(CanRxQueue == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* ========== 新增：检测Bus‑Off事件 ========== */
        if(g_can_busoff_flag != 0U)
        {
            FaultMgr_PostEvent(FAULT_CAN_BUS_OFF, 0, 0);
            bsp_can_busoff_recovery();
            g_can_busoff_flag = 0U;  //网关任务清除busoff标志
        }

        if(xQueueReceive(CanRxQueue, &raw_frame, pdMS_TO_TICKS(100)) == pdPASS)
        {
            /* 只处理29bit扩展帧，过滤标准帧；静默丢弃，不打印 */
            if(raw_frame.ide == 0U)
            {
                continue;
            }

            if(app_can_validate_frame(&raw_frame, &parsed) == 1U)
            {
                /* Skip frames from nodes disabled by node_enable_mask
                 * (bit (node_id-1) = node node_id). Keeps disabled nodes
                 * permanently offline and out of the node table. */
                if((g_sys_cfg.node_enable_mask & (1U << (parsed.node_id - 1U))) == 0U)
                {
                    continue;
                }

                uint8_t calc_crc = app_calc_crc8(parsed.payload,4U);
                if(calc_crc != parsed.crc8)
                {
                    g_can_stats.invalid_frame_count++;
                    /* node_id从1开始，数组下标 node_id‑1 */
                    SensorNode_t *p_node = &nodes[parsed.node_id - 1U];
                    p_node->crc_error_count++;
                    FaultMgr_PostEvent(FAULT_SENSOR_CRC, parsed.node_id, p_node->crc_error_count);
#if NODE_DEBUG_PRINT
                    App_ReportPrint("[NODE] node:%d crc check error\r\n", parsed.node_id);
#endif
                    continue;
                }

#if NODE_DEBUG_PRINT
                App_ReportPrint("[CAN] Node:%02X,Type:%02X,Seq:%02X,Payload:%02X %02X %02X %02X\r\n",
                parsed.node_id, parsed.msg_type, parsed.seq,
                parsed.payload[0],parsed.payload[1],parsed.payload[2],parsed.payload[3]);
#endif

                /* 解析Q100电压、温度、按键状态 */
                uint16_t volt_q100 = ((uint16_t)parsed.payload[0] << 8U) | parsed.payload[1];
                uint16_t temp_combine = ((uint16_t)parsed.payload[2] << 8U) | parsed.payload[3];
                uint8_t key_state = temp_combine & 0x03U;        /* 低2bit K1 K2按键 */
                int16_t temp_s16 = (int16_t)(temp_combine & 0xFFFCU); /* 高14bit温度 */

                SensorData_t sensor_data;
                sensor_data.adc0 = volt_q100;
                sensor_data.adc1 = (uint16_t)temp_s16;
                sensor_data.adc2 = 0U;
                sensor_data.key_state = key_state;

                /* 更新节点数据表 */
                App_UpdateNodeTable(parsed.node_id, parsed.seq, &sensor_data);

                /* 收到合法报文，总线正常，清除busoff重试计数 */
                g_can_stats.busoff_retry_cnt = 0U;
            }
            else
            {
                App_ReportPrint("[CAN] INVALID frame dropped (DLC=%u,id=0x%08lX)\r\n", raw_frame.dlc, raw_frame.id);
            }
        }
        else
        {
            /* Queue receive timed out (100ms no frame). Keep the gateway task
             * alive but do not print periodic debug logs; the SYS_MON LED/
             * heartbeat already proves the task is scheduled. This avoids UART
             * spontaneous traffic competing with host CLI commands. */
            (void)0;
        }
        /* 系统监控心跳，放到循环内部 */
        AppSysMon_HeartBeat(TASK_IDX_CAN_GW);
    }
}



