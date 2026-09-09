#include "app_can_task.h"
#include <stdio.h>
#include "bsp_gpio.h"
#include "app_main.h"

/**
 * @brief CAN业务任务，裸机分时调度，调用一次处理1帧，立刻返回，不阻塞
 */
void App_Can_Task(void)
{
    CAN_MSG_t can_rx_msg;
    if(BSP_CAN_GetRxMsg(&can_rx_msg) != 0)
    {
        DBG_PRINT("CAN Recv ID:0x%03X len:%d\r\n",can_rx_msg.id, can_rx_msg.len); //打印接收的ID和数据长度
        //打印原始数据
        for(int i=0;i<can_rx_msg.len;i++)
        {
            DBG_PRINT("%02X ",can_rx_msg.data[i]);
        }
        DBG_PRINT("\r\n");

        //只处理ID为0x200，数据长度为大于等于3的标准帧指令
        if(can_rx_msg.id == 0x200 && can_rx_msg.len >= 3)
        {
            //强转uint16，避免uint8相加溢出导致校验错误
            uint16_t calc_sum = (uint16_t)can_rx_msg.data[0] + (uint16_t)can_rx_msg.data[1];
            if(calc_sum == can_rx_msg.data[2])
            {
                // 只允许0、1两种状态指令，其他指令直接忽略
                if(can_rx_msg.data[0] <=1)
                {
                    BSP_GPIO_SetLed(LED_0, can_rx_msg.data[0]);
                }
                else if(can_rx_msg.data[1] <=1)
                {
                    BSP_GPIO_SetLed(LED_1, can_rx_msg.data[1]);
                }

                DBG_PRINT("CAN cmd execute ok\r\n");

            }
            else
            {
                DBG_PRINT("CAN checksum error, drop frame\r\n");
            }
        }
    }
}
