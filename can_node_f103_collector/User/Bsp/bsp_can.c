#include "bsp_can.h"
#include "main.h"
#include "task.h"
#include <string.h>

extern CAN_HandleTypeDef hcan;

QueueHandle_t can_rx_queue = NULL;
uint32_t g_can_err_cnt = 0U;
volatile uint8_t can_err_flag = 0U;

//CAN接收中断回调
/**
 * @brief CAN FIFO0接收中断回调，中断上下文！禁止printf、浮点、延时
 */
//CAN接收回调
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx_header;
    uint8_t rx_data[8];

    if(xQueueIsQueueFullFromISR(can_rx_queue))
    {
        return;
    }

    if(HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK)
    {
        //只接收【29bit扩展帧】，直接丢弃标准帧
        if(rx_header.IDE == CAN_ID_EXT)
        {
            CAN_MSG_t raw_msg;
            raw_msg.ext_id = rx_header.ExtId;
            raw_msg.len = rx_header.DLC;
            memcpy(raw_msg.data, rx_data, rx_header.DLC);
            xQueueSendFromISR(can_rx_queue, &raw_msg, NULL);
        }
    }
}

//CAN错误中断回调——诊断用：打印错误并统计
/**
 * @brief CAN错误中断回调，仅做计数，中断内禁止打印
 */
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
		g_can_err_cnt++;
    can_err_flag = 1U;

    //⚠️注意：Bus‑Off状态下 HAL_CAN_ResetError() 无效，不要在这里调用！
}

//读取 MCU 自身 CAN 状态（用于上位机打印）
void BSP_CAN_GetBusStatus(uint32_t *esr, uint8_t *lec)
{
    uint32_t val = hcan.Instance->ESR;
    if(esr) *esr = val;
    if(lec) *lec = (uint8_t)(val & 0x7U);
}

void BSP_CAN_Init(void)
{
    CAN_FilterTypeDef sFilterConfig = {0};
    HAL_StatusTypeDef ret;
    //使能CAN1 SCE中断（错误/状态变化）
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);

    //配置滤波器：接收所有标准ID报文
    //注意：滤波器必须在HAL_CAN_Start之前配置！
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    //重点：接收29bit扩展帧
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    ret = HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
    if(ret != HAL_OK)
    {
        Error_Handler();
    }
    ret = HAL_CAN_Start(&hcan);
    if(ret != HAL_OK)
    {
        Error_Handler();
    }
    
    //创建CAN接收队列： 8个报文缓存
    if(can_rx_queue == NULL)
    {
        can_rx_queue = xQueueCreate(8,sizeof(CAN_MSG_t));
    }
    
    //开启 FIFO0中断、总线关闭、错误码通知
    ret = HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING
                                            | CAN_IT_BUSOFF
                                            | CAN_IT_LAST_ERROR_CODE);
    if(ret != HAL_OK)
    {
        Error_Handler();
    }
}

//应用层调用：发送消息
/**
 * @brief CAN发送标准帧
 * @param std_id 标准ID
 * @param buf 数据缓冲区
 * @param len 长度0~8
 * @retval HAL_OK(0)成功；HAL_BUSY(2)邮箱满超时；HAL_ERROR(1)其他错误
 */
uint8_t BSP_CAN_SendMsg(uint32_t std_id,uint8_t *buf,uint8_t len)
{
    CAN_TxHeaderTypeDef tx_header = {0}; // 清零！避免TransmitGlobalTime栈垃圾
    uint32_t tx_mailbox = 0;
    HAL_StatusTypeDef ret;

    tx_header.StdId = std_id;
    tx_header.DLC = len;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.IDE = CAN_ID_STD;
    tx_header.TransmitGlobalTime = DISABLE;

    /* 第一次尝试入邮箱 */
    ret = HAL_CAN_AddTxMessage(&hcan, &tx_header, buf, &tx_mailbox);
    if(ret == HAL_OK)
    {
        return HAL_OK;
    }

    /* F1 HAL 在三个 TX mailbox 全满时返回 HAL_ERROR（不是 HAL_BUSY）。
     * 若检测到邮箱满，短等待重试，避免偶发的上一帧未发完就被判失败。 */
    if(HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0U)
    {
        uint32_t tick_start = HAL_GetTick();
        do {
            vTaskDelay(pdMS_TO_TICKS(1));
            ret = HAL_CAN_AddTxMessage(&hcan, &tx_header, buf, &tx_mailbox);
            if(ret == HAL_OK)
            {
                return HAL_OK;
            }
        } while((HAL_GetTick() - tick_start) < 10U);

        /* 10ms 后仍无空闲邮箱：总线大概率无 ACK，报文一直在重试 */
        return HAL_BUSY;
    }

    /* 参数错误、未初始化等其他错误 */
    return ret;
}

void BSP_CAN_FilterConfig(void)
{
  CAN_FilterTypeDef  sFilterConfig;

  sFilterConfig.FilterBank = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0x0000;
  sFilterConfig.FilterIdLow = 0x0000;
  sFilterConfig.FilterMaskIdHigh = 0x0000;
  sFilterConfig.FilterMaskIdLow = 0x0000;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;

  HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
}

/**
 * @brief CRC‑8 多项式0x07，与F4网关完全一致
 */
uint8_t app_calc_crc8(const uint8_t *buf, uint8_t len)
{
    uint8_t crc = 0U;
    for(uint8_t i=0; i<len; i++)
    {
        crc ^= buf[i];
        for(uint8_t bit=0; bit<8; bit++)
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
 * @brief 发送29bit扩展帧
 * @param ext_id 29bit扩展ID
 * @param data   报文数据
 * @param len    DLC(0~8)
 * @retval HAL_OK成功，其他失败
 */
uint8_t BSP_CAN_SendMsg_Ext(uint32_t ext_id, uint8_t *data, uint8_t len)
{
    CAN_TxHeaderTypeDef tx_header;
    uint32_t mailbox;

    tx_header.IDE = CAN_ID_EXT;
    tx_header.ExtId = ext_id;
    tx_header.DLC = len;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.TransmitGlobalTime = DISABLE;

    if(HAL_CAN_AddTxMessage(&hcan, &tx_header, data, &mailbox) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return HAL_OK;
}

