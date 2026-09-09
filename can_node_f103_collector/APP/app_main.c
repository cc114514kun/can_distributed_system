#include "app_main.h"
#include "bsp_can.h"
#include "main.h"
#include <stdio.h>
#include <string.h>   
#include "bsp_rtc.h"
#include "bsp_iwdg.h"
#include "bsp_adc.h"
#include "bsp_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#define NTC_TEMP_ALARM_THRESHOLD     55.0f    //超温预警阈值55℃

/* 全局状态：g_alarm_flag 在bsp_rtc.c定义，这里extern引用，禁止重复定义 */
extern volatile uint8_t g_alarm_flag;
uint8_t g_fault_code = 0; //故障码 0:无故障 1:有故障
extern volatile uint8_t can_err_flag;
extern CAN_HandleTypeDef hcan;
/* Keep periodic data and asynchronous events on independent sequence streams.
 * Otherwise an inserted alarm frame would look like a lost sensor frame. */
static uint16_t g_sensor_seq = 0U;
static uint16_t g_event_seq = 0U;
/* 任务句柄 */
TaskHandle_t app_task_handle = NULL;

//CAN发送统计（可调试是否丢包）
static uint32_t g_can_tx_total     = 0;
static uint32_t g_can_tx_fail      = 0;
static uint32_t g_can_tx_fail_busy = 0; // 邮箱满（无ACK导致报文占住邮箱重试）
static uint32_t g_can_tx_fail_err  = 0; // 其他错误（参数/未初始化等）

uint8_t can_err_buf[8] = {0};

//故障码定义
#define FAULT_NONE 0x00 //无故障
#define FAULT_NTC_OPEN      0x01  //NTC断线
#define FAULT_ADC_OVERFLOW  0x02  //ADC越界
#define FAULT_IWDG_RESET    0x04  //上次看门狗复位

/**
 * @brief 读取复位源，判断是否看门狗复位
 */
uint8_t BSP_RTC_CheckIWDGReset(void)
{
    uint8_t fault = 0;
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET)
    {
        fault |= FAULT_IWDG_RESET;
    }
    __HAL_RCC_CLEAR_RESET_FLAGS(); //清除复位标志
    return fault;
}

static void  APP_MainTask(void *pvParameters)
{
    (void)pvParameters;
	
		//====新增 NTC超温预警局部变量====
		static uint8_t ntc_overtemp_flag = 0U;     //超温标志，防止重复刷屏上报

	  //底层BSP初始化,任务底部，只执行一次初始化
    ADC_DMA_Init();    //必须最先启动ADC DMA采集，否则滤波全是0
    BSP_GPIO_Init();   //LED(PB5/PE5)推挽输出 + 按键(PA4/PA5)上拉输入
    BSP_CAN_Init();
    BSP_RTC_Init();
    BSP_IWDG_Init();

    //----------上电检测复位来源----------
    uint8_t rst_flag = BSP_RTC_CheckIWDGReset();
    if(rst_flag & FAULT_IWDG_RESET)
    {
        g_fault_code |= FAULT_IWDG_RESET;
        DBG_PRINT("\r\n!!! IWDG watchdog reset detected !!!\r\n");
    }

    //设置RTC闹钟A，50秒触发一次
    BSP_RTC_SetAlarmA(0,0,50);

    RTC_DateTime_t now;
    uint16_t adc1,adc2;
    float ntc_temp;
    uint8_t key1,key2;

    //非阻塞时间戳：200ms执行一次打印和CAN上报
    static uint32_t tick_last_report = 0U;
    const uint32_t REPORT_PERIOD_MS = 200U;

   CAN_MSG_t rx_msg;

    while(1)
    {
        //1.喂狗放在最前面，保证哪怕上报逻辑卡住也尽量喂狗
        BSP_IWDG_Feed();

				//========【CAN故障上报逻辑】========
        if(can_err_flag == 1U)
        {
            uint32_t esr_reg = hcan.Instance->ESR;

            uint8_t can_tx_buf[CAN_APP_FRAME_DLC] = {0};
            uint16_t event_seq = g_event_seq++;

            can_tx_buf[0] = (uint8_t)(event_seq >> 8U);
            can_tx_buf[1] = (uint8_t)event_seq;
            can_tx_buf[2] = (uint8_t)((esr_reg >> 24U) & 0xFFU); /* REC */
            can_tx_buf[3] = (uint8_t)((esr_reg >> 16U) & 0xFFU); /* TEC */
            can_tx_buf[4] = (uint8_t)(esr_reg & 0x07U);          /* LEC */
            can_tx_buf[5] = (uint8_t)((esr_reg >> 2U) & 0x01U); /* BOFF */
            can_tx_buf[6] = 0U;
            can_tx_buf[7] = app_calc_crc8(can_tx_buf, 7U);

            uint32_t ext_id = CAN_MAKE_EXTID(CAN_PRIORITY_ALARM,
                                              CAN_DEVICE_COLLECTOR,
                                              NODE_ID_F103,
                                              MSG_TYPE_BUS_ERR,
                                              SUB_TYPE_DEFAULT);
            BSP_CAN_SendMsg_Ext(ext_id, can_tx_buf, CAN_APP_FRAME_DLC);

            can_err_flag = 0U; //清除标志，防止重复上报

            //Bus‑Off 执行外设自恢复
            if(esr_reg & (1U << 2U))
            {
                HAL_CAN_Stop(&hcan);
                vTaskDelay(pdMS_TO_TICKS(50));
							
                HAL_CAN_Init(&hcan);
				/* 重点：重新初始化之后，过滤器必须重新执行一遍！ */
				BSP_CAN_FilterConfig();
							
                HAL_CAN_Start(&hcan);
                HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING
                                                    | CAN_IT_BUSOFF
                                                    | CAN_IT_LAST_ERROR_CODE);
            }
        }
        //=======================================
        //非阻塞读取CAN队列，0不阻塞主循环
        if(xQueueReceive(can_rx_queue, &rx_msg, 0) == pdPASS)
        {
            // rx_msg.ext_id：29bit扩展ID；rx_msg.len对应DLC
            DBG_PRINT("CAN RECV ext_id:0x%08lX len:%d\r\n",rx_msg.ext_id, rx_msg.len);

            /* V2.0 control frames use the common 8-byte payload envelope. */
            if(rx_msg.len != CAN_APP_FRAME_DLC)
            {
                continue;
            }

            uint8_t rx_device_type = CAN_ID_GET_DEVICE_TYPE(rx_msg.ext_id);
            uint8_t rx_node_id = CAN_ID_GET_NODE_ID(rx_msg.ext_id);
            uint8_t rx_msg_type = CAN_ID_GET_MSG_TYPE(rx_msg.ext_id);
            uint8_t rx_sub_type = CAN_ID_GET_SUB_TYPE(rx_msg.ext_id);

            /* CRC covers sequence and body (data[0..6]). */
            uint8_t calc_crc = app_calc_crc8(&rx_msg.data[0], 7U);
            uint8_t recv_crc = rx_msg.data[7];

            if(calc_crc != recv_crc)
            {
                DBG_PRINT("CAN RX CRC ERROR!\r\n");
                continue;
            }

            /* The NodeID in a downlink frame is the destination address. */
            if(rx_device_type == CAN_DEVICE_COLLECTOR &&
               (rx_node_id == NODE_ID_F103 || rx_node_id == NODE_ID_BROADCAST) &&
               rx_msg_type == MSG_TYPE_CTRL &&
               rx_sub_type == SUB_TYPE_CTRL_LED &&
               rx_msg.data[2] == CTRL_OPCODE_SET_LED)
            {
                uint8_t led_ctrl = rx_msg.data[3];
                if(led_ctrl == 1U)
                {
                    BSP_GPIO_SetLed(LED_0,1);
                    DBG_PRINT("LED0 ON\r\n");
                }
                else if(led_ctrl == 0U)
                {
                    BSP_GPIO_SetLed(LED_0,0);
                    DBG_PRINT("LED0 OFF\r\n");
                }
            }
        }

        //3.读取ADC滤波值、NTC温度、按键
        BSP_ADC_GetFilterValue(&adc1,&adc2);
        ntc_temp = BSP_NTC_CalcTemp(adc2);
        key1 = BSP_GPIO_ReadKey(KEY_0);
        key2 = BSP_GPIO_ReadKey(KEY_1);

        //4.故障诊断逻辑，每轮都检测
        g_fault_code &= (~(FAULT_NTC_OPEN | FAULT_ADC_OVERFLOW));

        if(adc2 <=10 || adc2 >= 4080 || ntc_temp < -30 || ntc_temp > 120)
        {
            g_fault_code |= FAULT_NTC_OPEN;
        }
        if(adc1 > 4095)
        {
            g_fault_code |= FAULT_ADC_OVERFLOW;
        }

				//============【NTC超温预警】============
                if(ntc_temp >= NTC_TEMP_ALARM_THRESHOLD)
                {
                    if(ntc_overtemp_flag == 0U)
                    {
                        ntc_overtemp_flag = 1U;

                        uint8_t can_tx_buf[CAN_APP_FRAME_DLC] = {0};
                        uint16_t event_seq = g_event_seq++;

                        int16_t temp_s16 = (int16_t)(ntc_temp *100.0f + 0.5f);
                        uint16_t temp_raw = (uint16_t)temp_s16;

                        can_tx_buf[0] = (uint8_t)(event_seq >> 8U);
                        can_tx_buf[1] = (uint8_t)event_seq;
                        can_tx_buf[2] = (uint8_t)(temp_raw >> 8U);
                        can_tx_buf[3] = (uint8_t)temp_raw;
                        can_tx_buf[4] = ALARM_CODE_OVER_TEMP;
                        can_tx_buf[5] = 0U;
                        can_tx_buf[6] = 0U;
                        can_tx_buf[7] = app_calc_crc8(can_tx_buf, 7U);

                        uint32_t ext_id = CAN_MAKE_EXTID(CAN_PRIORITY_ALARM,
                                                          CAN_DEVICE_COLLECTOR,
                                                          NODE_ID_F103,
                                                          MSG_TYPE_OVER_TEMP,
                                                          SUB_TYPE_DEFAULT);
                        BSP_CAN_SendMsg_Ext(ext_id, can_tx_buf, CAN_APP_FRAME_DLC);

                        DBG_PRINT("!!! NTC OVER TEMP ALARM, temp:%.2f\r\n",ntc_temp);
                    }
                }
				else
				{
						//温度回落清除告警标记，允许下次再次触发告警
						ntc_overtemp_flag = 0U;
				}
				//=====================================================
				
        //5.RTC闹钟事件检测
        if(g_alarm_flag == 1)
        {
            g_alarm_flag = 0;
            BSP_GPIO_SetLed(LED_0,1);
            DBG_PRINT("RTC ALARM TRIGGER !\r\n");
            BSP_RTC_SetAlarmAfterSec(50);
        }

        //6.故障LED控制
        if(g_fault_code != FAULT_NONE)
        {
            BSP_GPIO_SetLed(LED_1,1);
        }
        else
        {
            BSP_GPIO_SetLed(LED_1,0);
        }

        //=====================定时200ms执行：打印 + CAN上报=====================
        if( (HAL_GetTick() - tick_last_report) >= REPORT_PERIOD_MS )
        {
            tick_last_report = HAL_GetTick();

            //读取RTC时间戳（放到上报时刻读取，减少读取次数）
            BSP_RTC_GetDateTime(&now);

            float volt = (float)adc1 * 3.3f / 4095.0f;
            //串口打印输出
            DBG_PRINT("[%04d-%02d-%02d %02d:%02d:%02d] volt:%.2fV temp:%.2fC key1:%d key2:%d fault:0x%02X\r\n",
                   now.year,now.month,now.date,now.hour,now.min,now.sec,
                   volt,ntc_temp,key1,key2,g_fault_code);

            /* V2.0 sensor payload, all multi-byte fields are big-endian:
             * [0..1] sequence, [2..3] voltage Q100, [4..5] temperature Q100,
             * [6] key/fault status, [7] CRC8(data[0..6]). */
			uint8_t can_tx_buf[CAN_APP_FRAME_DLC] = {0};
			uint16_t volt_q100 = (uint16_t)(volt * 100.0f + 0.5f);
			int16_t  temp_s16  = (int16_t)(ntc_temp * 100.0f + (ntc_temp>=0?0.5f:-0.5f));
			uint16_t sensor_seq = g_sensor_seq++;
			
			// 组装按键状态 bit0:K1 key1, bit1:K2 key2
			uint8_t key_state = 0U;
			if(key1) key_state |= (1U << 0);
			if(key2) key_state |= (1U << 1);
			
			can_tx_buf[0] = (uint8_t)(sensor_seq >> 8U);
			can_tx_buf[1] = (uint8_t)sensor_seq;
			can_tx_buf[2] = (uint8_t)(volt_q100 >> 8U);
			can_tx_buf[3] = (uint8_t)volt_q100;
			uint16_t temp_raw = (uint16_t)temp_s16;
			can_tx_buf[4] = (uint8_t)(temp_raw >> 8U);
			can_tx_buf[5] = (uint8_t)temp_raw;
			can_tx_buf[6] = (uint8_t)((key_state & 0x03U) | ((g_fault_code & 0x3FU) << 2U));
			can_tx_buf[7] = app_calc_crc8(can_tx_buf, 7U);

			uint32_t ext_id = CAN_MAKE_EXTID(CAN_PRIORITY_DATA,
                                              CAN_DEVICE_COLLECTOR,
                                              NODE_ID_F103,
                                              MSG_TYPE_SENSOR,
                                              SUB_TYPE_SENSOR_ENV);
			g_can_tx_total++;
			uint8_t tx_ret = BSP_CAN_SendMsg_Ext(ext_id, can_tx_buf, CAN_APP_FRAME_DLC);
			
            if(tx_ret != HAL_OK)
            {
                g_can_tx_fail++;
                //BUSY统计等原有逻辑保留不变
                if(tx_ret == HAL_BUSY)
                {
                    g_can_tx_fail_busy++;
                }
                else
                {
                    g_can_tx_fail_err++;
                }
            }

            //每50次上报（约10秒）打印一次TX统计 + MCU自身CAN错误统计
            if((g_can_tx_total % 50U) == 0U)
            {
                uint32_t esr;
                uint8_t  lec;
                uint32_t free_level;
                BSP_CAN_GetBusStatus(&esr, &lec);
                free_level = HAL_CAN_GetTxMailboxesFreeLevel(&hcan);
                DBG_PRINT("CAN-STAT: total=%lu fail=%lu(busy=%lu err=%lu) free=%lu | ESR=0x%08lX LEC=%u\r\n",
                          (unsigned long)g_can_tx_total,
                          (unsigned long)g_can_tx_fail,
                          (unsigned long)g_can_tx_fail_busy,
                          (unsigned long)g_can_tx_fail_err,
                          (unsigned long)free_level,
                          (unsigned long)esr,
                          (unsigned int)lec);
                //LEC参考：
                // 0=无错误 1=位错 2=格式错 3=CRC错 4=应答错 5=位反相 6=格式错 7=其他
            }
        }

        // FreeRtos让出CPU，不裸跑死循环占满CPU
				vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void App_SystemInit(void)
{
    // 创建业务任务（xTaskCreate）
    // 返回 pdPASS(1) 表示成功；失败通常是 configTOTAL_HEAP_SIZE 太小导致分配不出栈
    BaseType_t ret = xTaskCreate(APP_MainTask,
	              "AppMainTask",
								1024,
								NULL,
								2,
								&app_task_handle);
    if(ret != pdPASS)
    {
        // 任务创建失败：多数情况是 FreeRTOS 堆不足，需调大 configTOTAL_HEAP_SIZE
        DBG_PRINT("ERR: xTaskCreate AppMainTask FAILED (heap too small? increase configTOTAL_HEAP_SIZE)\r\n");
    }
}

// 栈溢出钩子
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    while(1);
}

// malloc失败钩子
void vApplicationMallocFailedHook(void)
{
    while(1);
}
