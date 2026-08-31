#ifndef __APP_MAIN_H
#define __APP_MAIN_H

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t AppMainTask_Handle;

typedef struct
{
    float volt;
    float temp;
    uint8_t key1;
    uint8_t key2;
    uint8_t fault;
}SENSOR_DATA_t;



void App_SystemInit(void);
void App_DebugPrintTask(void *arg);

/* ?????? */
extern void TaskCanGateway(void *arg);
extern void App_NodeMonitorTask(void *arg);

#endif
