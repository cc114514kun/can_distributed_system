#ifndef __BSP_RTC_H
#define __BSP_RTC_H

#include "stm32f1xx_hal.h"

//RTC日期时间结构体
typedef struct
{
    uint16_t year; //年份
    uint8_t  month; //月份
    uint8_t  date; //日期
    uint8_t  week; //星期
    uint8_t  hour; //小时
    uint8_t  min; //分钟
    uint8_t  sec; //秒
}RTC_DateTime_t;

extern volatile uint8_t g_alarm_flag;

void BSP_RTC_Init(void); //初始化RTC
void BSP_RTC_SetDateTime(RTC_DateTime_t *dt); //设置RTC日期时间
void BSP_RTC_GetDateTime(RTC_DateTime_t *dt); //获取RTC日期时间
void BSP_RTC_SetAlarmA(uint8_t hour, uint8_t min, uint8_t sec); //设置RTC闹钟时间
void BSP_RTC_SetAlarmAfterSec(uint32_t sec); //设置RTC间隔闹钟，间隔sec秒之后触发闹钟A
#endif
