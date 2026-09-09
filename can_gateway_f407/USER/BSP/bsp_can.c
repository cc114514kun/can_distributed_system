#include "bsp_can.h"
#include "stm32f4xx_hal_can.h" /* CAN_FilterTypeDef / CAN_TxHeaderTypeDef */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fault_mgr.h"
#include "app_sys_mon.h"
#include "app_uart_cli.h"
#include "app_config.h" /* g_sys_cfg: node_timeout_ms / temp_high_limit / report_period_ms / node_enable_mask */

QueueHandle_t CanRxQueue = NULL;
volatile uint8_t g_can_busoff_flag = 0U; /* set in ISR, cleared by TaskCanGateway */
SensorNode_t nodes[MAX_NODE_NUM];
extern CAN_HandleTypeDef hcan1;
CanStats_t g_can_stats = {0U};

/**
* @brief F3.1 Convert raw ADC counts to physical units (volts, NTC temp).
*/
static void app_raw_to_phys(const SensorData_t *raw, PhysData_t *phys_out)
{
    if(raw == NULL || phys_out == NULL)
    {
        return;
    }
    phys_out->voltage = (float)raw->adc0 / 100.0f;
    int16_t temp_s16 = (int16_t)raw->adc1;
    phys_out->temperature = (float)temp_s16 / 100.0f;
}

/**
* @brief F3.2 Threshold check; returns true on alarm.
*/
static bool app_check_alarm(const PhysData_t *phys)
{
    if(phys == NULL)
    {
        return false;
    }
    bool alarm = false;
    /* Temperature high-limit comes from g_sys_cfg (Q100: 8000 = 80.00C). */
    float temp_hi = (float)g_sys_cfg.temp_high_limit / 100.0f;
    if(phys->temperature > temp_hi)
    {
        alarm = true;
    }
    if(phys->voltage < VOLT_MIN || phys->voltage > VOLT_MAX)
    {
        alarm = true;
    }
    return alarm;
}

/**
* @brief F3.4 Init ring buffer.
*/
static void ringbuf_init(SensorRingBuf_t *ring)
{
    if(ring == NULL) return;
    ring->wr_idx = 0U;
    ring->rd_idx = 0U;
    ring->sample_cnt = 0U;
    ring->overwrite_cnt = 0U;
    memset(ring->buf, 0, sizeof(ring->buf));
}

/**
* @brief F3.4 Put sample; overwrites oldest when full.
*/
static void ringbuf_put(SensorRingBuf_t *ring, const PhysData_t *sample)
{
    if(ring == NULL || sample == NULL) return;
    if(ring->sample_cnt >= RING_BUF_SAMPLE_CNT)
    {
        ring->rd_idx = (ring->rd_idx + 1U) % RING_BUF_SAMPLE_CNT;
        ring->sample_cnt--;
        ring->overwrite_cnt++;
    }
    ring->buf[ring->wr_idx] = *sample;
    ring->wr_idx = (ring->wr_idx + 1U) % RING_BUF_SAMPLE_CNT;
    ring->sample_cnt++;
}

static bool __attribute__((unused)) ringbuf_get(SensorRingBuf_t *ring, PhysData_t *sample_out)
{
    if(ring == NULL || sample_out == NULL || ring->sample_cnt == 0U)
    {
        return false;
    }
    *sample_out = ring->buf[ring->rd_idx];
    ring->rd_idx = (ring->rd_idx + 1U) % RING_BUF_SAMPLE_CNT;
    ring->sample_cnt--;
    return true;
}

HAL_StatusTypeDef bsp_can_init(void)
{
    /* Create CAN RX queue, holds CanFrame_t. */
    CanRxQueue = xQueueCreate(CAN_RX_QUEUE_LEN, sizeof(CanFrame_t));
    if(CanRxQueue == NULL)
    {
        return HAL_ERROR;
    }
    /* CAN filter: bxCAN filter banks are deactivated by default.
    * mask = 0 means accept all IDs (std + ext) for debugging.
    * Real filter (29bit ext ID by NodeID/MsgType) can be added later.
    * Must be placed between HAL_CAN_Init and HAL_CAN_Start. */
    CAN_FilterTypeDef sFilterConfig = {0};
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    sFilterConfig.FilterActivation = CAN_FILTER_ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;
    if(HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK)
    {
        return HAL_ERROR;
    }
    /* Enable FIFO0 / error / bus-off / error-passive notifications. */
    HAL_CAN_ActivateNotification(&hcan1,
    CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_BUSOFF |
    CAN_IT_ERROR | CAN_IT_ERROR_PASSIVE);
    if(HAL_CAN_Start(&hcan1) != HAL_OK)
    {
        /* On failure undo the activation to avoid stray IRQs. */
        HAL_CAN_DeactivateNotification(&hcan1,
        CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_BUSOFF |
        CAN_IT_ERROR | CAN_IT_ERROR_PASSIVE);
        return HAL_ERROR;
    }
    return HAL_OK;
}

/* Bus-Off recovery (call from task context, not ISR). */
HAL_StatusTypeDef bsp_can_busoff_recovery(void)
{
    if(g_can_stats.busoff_retry_cnt >= CAN_BUSOFF_MAX_RETRY)
    {
        FaultMgr_PostEvent(FAULT_SYSTEM_FATAL, 0U, g_can_stats.busoff_retry_cnt);
        return HAL_ERROR;
    }

    g_can_stats.busoff_retry_cnt++;

    HAL_CAN_Stop(&hcan1);
    vTaskDelay(pdMS_TO_TICKS(100U));

    HAL_CAN_Init(&hcan1);

    /* 内联过滤器配置，移除不存在的bsp_can_config_filter */
    CAN_FilterTypeDef sFilterConfig = {0};
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    sFilterConfig.FilterActivation = CAN_FILTER_ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;
    HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig);

    HAL_StatusTypeDef ret = HAL_CAN_Start(&hcan1);
    if(ret == HAL_OK)
    {
        /* Re-enable RX/error/bus-off interrupts cleared by Stop+Init. */
        HAL_CAN_ActivateNotification(&hcan1,
            CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_BUSOFF |
            CAN_IT_ERROR | CAN_IT_ERROR_PASSIVE);
        g_can_stats.busoff_retry_cnt = 0U;
    }
    return ret;
}

/* Generic CAN send. id > 0x7FF -> 29bit ext; otherwise 11bit std. */
HAL_StatusTypeDef bsp_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if(data == NULL || len > 8U)
    {
        return HAL_ERROR;
    }
    CAN_TxHeaderTypeDef tx_header;
    tx_header.DLC = len;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.TransmitGlobalTime = DISABLE;
    if(id > 0x7FFU)
    {
        tx_header.IDE = CAN_ID_EXT;
        tx_header.ExtId = id;
    }
    else
    {
        tx_header.IDE = CAN_ID_STD;
        tx_header.StdId = id;
    }
    uint32_t mailbox = 0U;
    HAL_StatusTypeDef ret = HAL_CAN_AddTxMessage(&hcan1, &tx_header, (uint8_t *)data, &mailbox);
    if(ret != HAL_OK)
    {
        g_can_stats.send_fail_count++;
    }
    else
    {
        g_can_stats.tx_count++;
    }
    return ret;
}

/* F4.x Internal loopback self-test:
* 1. Switch to LOOPBACK mode (TX internally routed to RX, no TJA1050 / wiring).
* 2. Send a frame with the target id, wait 200ms for it in CanRxQueue.
* 3. Switch back to NORMAL mode and re-apply the filter.
* Returns HAL_OK on success, HAL_ERROR / TIMEOUT on link failure.
* Variables are pre-declared at the top to avoid goto‑skip‑init warnings. */
HAL_StatusTypeDef bsp_can_loopback_test(uint32_t id, const uint8_t *data, uint8_t len)
{
    if(data == NULL || len == 0U || len > 8U)
    {
        return HAL_ERROR;
    }
    CAN_HandleTypeDef *hc = &hcan1;
    CanFrame_t frame;
    /* Pre‑declared to avoid 546‑D "bypasses initialization" warning. */
    BaseType_t qret = pdFAIL;
    HAL_StatusTypeDef tx_ret = HAL_ERROR;
    /* 1. Switch to Loopback. */
    (void)HAL_CAN_Stop(hc);
    hc->Init.Mode = CAN_MODE_LOOPBACK;
    if(HAL_CAN_Init(hc) != HAL_OK)
    {
        App_ReportPrint("[CAN-LOOP] HAL_CAN_Init(LOOPBACK) FAILED\r\n");
        return HAL_ERROR;
    }
    /* 2. Init cleared filters, re‑apply (mask = 0 → accept all IDs). */
    CAN_FilterTypeDef sFilter = {0};
    sFilter.FilterBank = 0;
    sFilter.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilter.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilter.FilterIdHigh = 0;
    sFilter.FilterIdLow = 0;
    sFilter.FilterMaskIdHigh = 0;
    sFilter.FilterMaskIdLow = 0;
    sFilter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    sFilter.FilterActivation = CAN_FILTER_ENABLE;
    sFilter.SlaveStartFilterBank = 14;
    if(HAL_CAN_ConfigFilter(hc, &sFilter) != HAL_OK)
    {
        App_ReportPrint("[CAN-LOOP] HAL_CAN_ConfigFilter FAILED\r\n");
        return HAL_ERROR;
    }
    if(HAL_CAN_Start(hc) != HAL_OK)
    {
        App_ReportPrint("[CAN-LOOP] HAL_CAN_Start(LOOPBACK) FAILED\r\n");
        return HAL_ERROR;
    }
    /* Re‑activate FIFO0 / error notifications after Start. */
    HAL_CAN_ActivateNotification(hc, CAN_IT_RX_FIFO0_MSG_PENDING |
    CAN_IT_ERROR | CAN_IT_BUSOFF);
    /* 3. Build TX header and send. */
    CAN_TxHeaderTypeDef txh = {0};
    txh.RTR = CAN_RTR_DATA;
    txh.DLC = len;
    txh.IDE = (id > 0x7FFU) ? CAN_ID_EXT : CAN_ID_STD;
    txh.StdId = (id > 0x7FFU) ? 0U : id;
    txh.ExtId = (id > 0x7FFU) ? id : 0U;
    uint32_t tx_mailbox = 0U;
    tx_ret = HAL_CAN_AddTxMessage(hc, &txh, (uint8_t *)data, &tx_mailbox);
    {
        char lbuf[64];
        int loff = snprintf(lbuf, sizeof(lbuf), "[CAN-LOOP] TX id=0x%08lX D:", id);
        for(uint8_t i = 0; i < len && (size_t)loff < sizeof(lbuf) - 4; i++)
        {
            loff += snprintf(lbuf + loff, sizeof(lbuf) - (size_t)loff, "%02X ", data[i]);
        }
        (void)snprintf(lbuf + loff, sizeof(lbuf) - (size_t)loff, "=> %s\r\n",
                       (tx_ret == HAL_OK) ? "OK" : "FAIL");
        App_ReportPrint("%s", lbuf);
    }
    /* 4. Wait for loopback frame, if TX was OK. */
    if(tx_ret == HAL_OK)
    {
        TickType_t wait_ticks = pdMS_TO_TICKS(200U);
        qret = xQueueReceive(CanRxQueue, &frame, wait_ticks);
        if(qret == pdPASS)
        {
            {
                char lbuf[64];
                int loff = snprintf(lbuf, sizeof(lbuf),
                    "[CAN-LOOP] RX OK id=0x%08lX ide=%u dlc=%u D:",
                    frame.id, frame.ide, frame.dlc);
                for(uint8_t i = 0; i < frame.dlc && (size_t)loff < sizeof(lbuf) - 4; i++)
                {
                    loff += snprintf(lbuf + loff, sizeof(lbuf) - (size_t)loff, "%02X ", frame.data[i]);
                }
                (void)snprintf(lbuf + loff, sizeof(lbuf) - (size_t)loff, "\r\n");
                App_ReportPrint("%s", lbuf);
            }
        }
        else
        {
            App_ReportPrint("[CAN-LOOP] RX TIMEOUT (200ms no loopback frame)\r\n");
        }
    }
    /* 5. Restore NORMAL mode for normal operation. */
    (void)HAL_CAN_Stop(hc);
    hc->Init.Mode = CAN_MODE_NORMAL;
    if(HAL_CAN_Init(hc) == HAL_OK &&
    HAL_CAN_ConfigFilter(hc, &sFilter) == HAL_OK &&
    HAL_CAN_Start(hc) == HAL_OK)
    {
        HAL_CAN_ActivateNotification(hc, CAN_IT_RX_FIFO0_MSG_PENDING |
        CAN_IT_ERROR | CAN_IT_BUSOFF);
        App_ReportPrint("[CAN-LOOP] restored to NORMAL mode\r\n");
    }
    else
    {
        App_ReportPrint("[CAN-LOOP] WARN: restore to NORMAL FAILED, please reset MCU\r\n");
        return HAL_ERROR;
    }
    return (qret == pdPASS) ? HAL_OK : HAL_ERROR;
}

/* CAN RX FIFO0 ISR callback.
* Inside ISR: read frame, timestamp, enqueue, update stats.
* No parsing, no printf allowed in ISR. */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx_header;
    CanFrame_t frame;
    uint8_t rx_data[8];
    if(HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK)
    {
        return;
    }
    /* Mark frame type; std frames also accepted (no longer silent drop). */
    frame.ide = (rx_header.IDE == CAN_ID_EXT) ? 1U : 0U;
    if(rx_header.IDE == CAN_ID_EXT)
    {
        frame.id = rx_header.ExtId;
    }
    else
    {
        frame.id = (uint32_t)rx_header.StdId;
    }
    /* Fill rest of frame. */
    frame.dlc = rx_header.DLC;
    frame.timestamp = HAL_GetTick();
    for(int i=0; i<8; i++)
    {
        frame.data[i] = rx_data[i];
    }
    g_can_stats.rx_count++;
    /* Use xQueueSendFromISR to enqueue from ISR. */
    BaseType_t higher_priority_task_woken = pdFALSE;
    if(xQueueSendFromISR(CanRxQueue, &frame, &higher_priority_task_woken) != pdPASS)
    {
        /* Queue full, count overflow. */
        g_can_stats.queue_overflow_count++;
        FaultMgr_PostEvent(FAULT_CAN_RX_OVERFLOW, 0xFF, g_can_stats.queue_overflow_count);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* CAN error ISR callback (Bus‑Off / passive error). */
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    if(hcan->Instance == CAN1)
    {
        if(__HAL_CAN_GET_FLAG(hcan, CAN_FLAG_BOF))
        {
            __HAL_CAN_CLEAR_FLAG(hcan, CAN_FLAG_BOF);
            g_can_busoff_flag = 1U; /* set in ISR; TaskCanGateway recovers in task context */
            FaultMgr_PostEvent(FAULT_CAN_BUS_OFF,0,0);
        }
    }
}

/**
* @brief Init all node table entries. Called once at gateway startup.
*/
void App_NodeTable_Init(void)
{
    for(uint8_t i = 0; i < MAX_NODE_NUM; i++)
    {
        nodes[i].node_id = i + 1U; /* slave id 1..MAX_NODE_NUM */
        nodes[i].online = false; /* offline at startup */
        nodes[i].alarm = false; /* no alarm at startup */
        nodes[i].rx_count = 0U; /* RX counter */
        nodes[i].lost_count = 0U; /* lost frame counter */
        nodes[i].crc_error_count= 0U; /* CRC error counter */
        nodes[i].offline_count = 0U; /* offline counter */
        nodes[i].recovery_count = 0U; /* recovery counter */
        nodes[i].last_rx_tick = xTaskGetTickCount();
        nodes[i].last_seq = 0U;
        nodes[i].seq_valid = false;
        nodes[i].data.adc0 = 0U;
        nodes[i].data.adc1 = 0U;
        nodes[i].data.adc2 = 0U;
        nodes[i].data.key_state = 0U;
        nodes[i].data.fault_code = 0U;
        memset(&nodes[i].phys_data, 0, sizeof(PhysData_t));
        ringbuf_init(&nodes[i].ring_buf); /* init each node ring buffer */
    }
}

/**
* @brief Update node table when a valid slave frame is received.
* @param node_id slave id (1..MAX_NODE_NUM)
* @param seq frame sequence number
* @param p_sensor decoded sensor data
*/
void App_UpdateNodeTable(uint8_t node_id, uint16_t seq, const SensorData_t *p_sensor)
{
    if(node_id == 0 || node_id > MAX_NODE_NUM)
    {
        return;
    }
    SensorNode_t *p_node = &nodes[node_id - 1U];
    /* Recovery: was offline, now receiving → mark online. */
    if(p_node->online == false)
    {
        p_node->online = true;
        p_node->recovery_count++;
        #if NODE_DEBUG_PRINT
        App_ReportPrint("[NODE] node:%d RECOVERY, recovery_cnt:%lu\r\n", node_id, p_node->recovery_count);
        #endif
    }
    /* The first frame establishes the baseline. Subsequent subtraction is
     * naturally wrap-safe for uint16_t sequence numbers. A delta in the
     * backward half-range is treated as stale/out-of-order, not massive loss. */
    if(p_node->seq_valid == false)
    {
        p_node->seq_valid = true;
        p_node->last_seq = seq;
    }
    else
    {
        uint16_t seq_diff = (uint16_t)(seq - p_node->last_seq);
        if(seq_diff > 1U && seq_diff < 0x8000U)
        {
            p_node->lost_count += (seq_diff - 1U);
        #if NODE_DEBUG_PRINT
            App_ReportPrint("[NODE] node:%d lost frame, lost_cnt:%lu\r\n",
                            node_id, p_node->lost_count);
        #endif
        }
        /* Only move the baseline forward. Duplicate or stale/out-of-order
         * frames must not make the following valid frame look lost. */
        if(seq_diff > 0U && seq_diff < 0x8000U)
        {
            p_node->last_seq = seq;
        }
    }
    /* Update stats, timestamp, raw data. */
    p_node->rx_count++;
    p_node->last_rx_tick = xTaskGetTickCount();
    p_node->data.adc0 = p_sensor->adc0;
    p_node->data.adc1 = p_sensor->adc1;
    p_node->data.adc2 = p_sensor->adc2;
    p_node->data.key_state = p_sensor->key_state;
    p_node->data.fault_code = p_sensor->fault_code;
    /* ===== Phase 3 business logic ===== */
    /* F3.1 Convert raw ADC to physical units (voltage, temperature). */
    app_raw_to_phys(&p_node->data, &p_node->phys_data);
    /* F3.2 Threshold check, set alarm flag. */
    p_node->alarm = app_check_alarm(&p_node->phys_data);
    if(p_node->alarm == true)
    {
        /* Temperature / voltage out of range alarm. */
        FaultMgr_PostEvent(FAULT_SENSOR_SEQUENCE, node_id, 0U);
    }
    /* F3.4 Push latest physical sample into node ring buffer. */
    ringbuf_put(&p_node->ring_buf, &p_node->phys_data);
    /* ================================== */

    /* 收到有效报文，总线正常，清空Bus‑Off重试计数 */
    g_can_stats.busoff_retry_cnt = 0U;
}

/**
* @brief Node monitor task. Scan period and offline timeout both come from
*        g_sys_cfg (report_period_ms / node_timeout_ms). Disabled nodes
*        (bit not set in node_enable_mask) are skipped entirely.
*/
void App_NodeMonitorTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t now_tick;

    for(;;)
    {
        /* Re-read config every iteration so SET_CONFIG takes effect live
           (no reset needed). Clamp to sane ranges. */
        uint32_t scan_ms = g_sys_cfg.report_period_ms;
        if(scan_ms < 20U)  scan_ms = 20U;     /* scan at least every 20ms */
        if(scan_ms > 1000U) scan_ms = 1000U;  /* but not slower than 1s */
        TickType_t scan_tick = pdMS_TO_TICKS(scan_ms);

        uint32_t timeout_ms = g_sys_cfg.node_timeout_ms;
        if(timeout_ms < 50U) timeout_ms = 50U; /* minimum offline timeout */
        TickType_t offline_tick = pdMS_TO_TICKS(timeout_ms);

        now_tick = xTaskGetTickCount();
        for(uint8_t i = 0; i < MAX_NODE_NUM; i++)
        {
            /* Skip nodes disabled by node_enable_mask (bit i = node i+1). */
            if((g_sys_cfg.node_enable_mask & (1U << i)) == 0U)
            {
                continue;
            }
            SensorNode_t *p_node = &nodes[i];
            if(p_node->online == true)
            {
                if( (now_tick - p_node->last_rx_tick) > offline_tick )
                {
                    p_node->online = false;
                    p_node->offline_count++;
                    /* Post fault event for node offline. */
                    FaultMgr_PostEvent(FAULT_NODE_OFFLINE, p_node->node_id, p_node->offline_count);
                    #if NODE_DEBUG_PRINT
                    App_ReportPrint("[NODE] node:%d OFFLINE, offline_cnt:%lu\r\n",
                    p_node->node_id, p_node->offline_count);
                    #endif
                }
            }
        }
        AppSysMon_HeartBeat(TASK_IDX_NODE_MON);
        vTaskDelay(scan_tick);
    }
}

/* F4.y bxCAN hardware self‑diagnose:
* Print key registers to determine whether F407 is actually on the bus.
* - MSR.INAK (bit0) must be 0 (entered NORMAL / active)
* - MSR.SLAK (bit1) must be 0 (not in SLEEP)
* - ESR should be 0; if REC != 0 we've already seen error frames
* - BTR reflects current baud rate divider and bit timing */
void bsp_can_self_diag(void)
{
    /* Diagnostic registers are no longer printed to UART by default:
     * startup logs were flooding the report queue and delaying OK responses.
     * Read registers directly via a debugger or re-enable prints here when
     * actively debugging CAN physical-layer issues. */
    (void)hcan1.Instance;
}

