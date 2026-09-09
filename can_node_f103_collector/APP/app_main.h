#ifndef __APP_MAIN_H
#define __APP_MAIN_H

#include "bsp_can.h"
#include "stm32f1xx_hal.h"

//====调试打印宏开关====
#define DEBUG_PRINT     1   //1打开打印；0关闭所有printf输出
#if DEBUG_PRINT
#include <stdio.h>
#define DBG_PRINT(fmt, ...)     printf(fmt,##__VA_ARGS__)
#else
#define DBG_PRINT(fmt, ...)
#endif

extern uint8_t g_fault_code;
void App_SystemInit(void);

#endif
