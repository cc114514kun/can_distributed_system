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
 * @brief Validate a V2.0 frame and decode its ID plus common payload envelope.
 * @retval 1:校验通过 0:校验失败
 */
uint8_t app_can_validate_frame(const CanFrame_t *raw, CanParsedFrame_t *out)
{
    if(raw == NULL || out == NULL)
        return 0;

    if(raw->dlc != CAN_APP_FRAME_DLC || raw->id > CAN_EXT_ID_MASK)
    {
        g_can_stats.invalid_frame_count++;
        FaultMgr_PostEvent(FAULT_CAN_ILLEGAL_FRAME, 0xFF, raw->dlc);
        return 0;
    }

    out->priority = CAN_ID_GET_PRIORITY(raw->id);
    out->device_type = CAN_ID_GET_DEVICE_TYPE(raw->id);
    out->node_id = CAN_ID_GET_NODE_ID(raw->id);
    out->msg_type = CAN_ID_GET_MSG_TYPE(raw->id);
    out->sub_type = CAN_ID_GET_SUB_TYPE(raw->id);
    out->seq = ((uint16_t)raw->data[0] << 8U) | raw->data[1];

    /* 节点ID合法性校验 */
    if(out->device_type != CAN_DEVICE_COLLECTOR ||
       out->node_id == 0U || out->node_id > MAX_NODE_NUM)
    {
        g_can_stats.invalid_frame_count++;
        FaultMgr_PostEvent(FAULT_CAN_ILLEGAL_FRAME, out->node_id, 0U);
        return 0;
    }

    out->crc8 = raw->data[7];
    if(app_calc_crc8(raw->data, 7U) != out->crc8)
    {
        SensorNode_t *p_node = &nodes[out->node_id - 1U];
        g_can_stats.invalid_frame_count++;
        p_node->crc_error_count++;
        FaultMgr_PostEvent(FAULT_SENSOR_CRC,
                           out->node_id,
                           p_node->crc_error_count);
        return 0U;
    }

    for(uint8_t i = 0U; i < 5U; i++)
    {
        out->payload[i] = raw->data[i + 2U];
    }

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

#if NODE_DEBUG_PRINT
                App_ReportPrint("[CAN] P:%u Dev:%u Node:%u Type:%02X Sub:%u Seq:%u\r\n",
                    parsed.priority, parsed.device_type, parsed.node_id,
                    parsed.msg_type, parsed.sub_type, parsed.seq);
#endif

                switch(parsed.msg_type)
                {
                    case MSG_TYPE_SENSOR:
                    {
                        if(parsed.sub_type != SUB_TYPE_SENSOR_ENV)
                        {
                            FaultMgr_PostEvent(FAULT_CAN_PROTOCOL,
                                               parsed.node_id,
                                               parsed.sub_type);
                            break;
                        }

                        SensorData_t sensor_data;
                        sensor_data.adc0 = ((uint16_t)parsed.payload[0] << 8U) |
                                           parsed.payload[1];
                        sensor_data.adc1 = ((uint16_t)parsed.payload[2] << 8U) |
                                           parsed.payload[3];
                        sensor_data.adc2 = 0U;
                        sensor_data.key_state = parsed.payload[4] & 0x03U;
                        sensor_data.fault_code = (parsed.payload[4] >> 2U) & 0x3FU;

                        App_UpdateNodeTable(parsed.node_id, parsed.seq, &sensor_data);
                        break;
                    }

                    case MSG_TYPE_OVER_TEMP:
                    {
                        int16_t temp_q100 = (int16_t)(
                            ((uint16_t)parsed.payload[0] << 8U) |
                            parsed.payload[1]);
                        FaultMgr_PostEvent(FAULT_SENSOR_ALARM,
                                           parsed.node_id,
                                           (uint32_t)(uint16_t)temp_q100);
                        break;
                    }

                    case MSG_TYPE_BUS_ERR:
                    {
                        uint32_t bus_error = ((uint32_t)parsed.payload[0] << 24U) |
                                             ((uint32_t)parsed.payload[1] << 16U) |
                                             ((uint32_t)parsed.payload[2] << 8U) |
                                             parsed.payload[3];
                        FaultMgr_PostEvent(FAULT_NODE_CAN_ERROR,
                                           parsed.node_id,
                                           bus_error);
                        break;
                    }

                    default:
                        FaultMgr_PostEvent(FAULT_CAN_PROTOCOL,
                                           parsed.node_id,
                                           parsed.msg_type);
                        break;
                }

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



