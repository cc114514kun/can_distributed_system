#ifndef __BSP_GPIO_H
#define __BSP_GPIO_H

#include "stm32f4xx_hal.h"

/* LED?? PF9 PF10 */
#define LED_RUN_PORT        GPIOF
#define LED_RUN_PIN         GPIO_PIN_9

#define LED_CAN_PORT        GPIOF
#define LED_CAN_PIN         GPIO_PIN_10

/* ??? PF8 */
#define BEEP_PORT           GPIOF
#define BEEP_PIN            GPIO_PIN_8

/* ???? PE2 S2 */
#define KEY_USER_PORT       GPIOE
#define KEY_USER_PIN        GPIO_PIN_2

void BSP_GPIO_Init(void);

/**
 * @brief ??LED
 * @param pin:LED_PIN
 * @param level:1? 0?
 */
void BSP_Led_Set(uint16_t pin, uint8_t level);
void BSP_Led_Toggle(uint16_t pin);

/**
 * @brief ???
 * @param level:1? 0??
 */
void BSP_Beep_Set(uint8_t level);
void BSP_Beep_Toggle(void);

/**
 * @retval 1??,0??
 */
uint8_t BSP_Key_Read(void);

#endif


