#ifndef __BSP_IWDG_H
#define __BSP_IWDG_H

#include "stm32f1xx_hal.h"

void BSP_IWDG_Init(void); //初始化看门狗
void BSP_IWDG_Feed(void); //喂狗看门狗

#endif
