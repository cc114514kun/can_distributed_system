#ifndef __APP_UART_CLI_H
#define __APP_UART_CLI_H

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include <stdint.h>

/* Firmware Product Info Phase10 */
#define PRODUCT_NAME        "CAN Sensor Gateway"
#define HW_VER_STR          "F407‑GW‑V1.0"
#define FW_VER_STR          "V1.2.0"
#define PROTOCOL_VER_STR    "V1.0"
#define BUILD_DATE_STR      __DATE__   //编译器内置宏，自动取编译日期


#define REPORT_LINE_MAX_LEN     128U
#define REPORT_QUEUE_LEN        64U

BaseType_t App_ReportPrint(const char *fmt, ...);
void AppCli_Init(UART_HandleTypeDef *huart);


#endif


