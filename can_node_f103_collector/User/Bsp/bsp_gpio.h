#ifndef __BSP_GPIO_H
#define __BSP_GPIO_H

#include "stm32f1xx_hal.h"

#define KEY_0   0
#define KEY_1   1

#define LED_0   0
#define LED_1   1

void    BSP_GPIO_Init(void);             //GPIO初始化（LED推挽输出+按键上拉输入）
uint8_t BSP_GPIO_ReadKey(uint8_t key_id);
void    BSP_GPIO_SetLed(uint8_t led_id, uint8_t state);


#endif
