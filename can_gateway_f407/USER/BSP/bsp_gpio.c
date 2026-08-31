#include "bsp_gpio.h"

void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_conf = {0};

    /* ??GPIO?? */
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PF8 PF9 PF10 ???? */
    gpio_conf.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_conf.Pull = GPIO_NOPULL;
    gpio_conf.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_conf.Pin = BEEP_PIN | LED_RUN_PIN | LED_CAN_PIN;
    HAL_GPIO_Init(GPIOF, &gpio_conf);

    /* PE2 ?? ???? */
    gpio_conf.Mode = GPIO_MODE_INPUT;
    gpio_conf.Pull = GPIO_PULLUP;
    gpio_conf.Pin = KEY_USER_PIN;
    HAL_GPIO_Init(KEY_USER_PORT, &gpio_conf);

    /* ????????:LED????,?????,???CubeMX?? */
    HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_CAN_PORT, LED_CAN_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BEEP_PORT, BEEP_PIN, GPIO_PIN_RESET);
}

void BSP_Led_Set(uint16_t pin, uint8_t level)
{
    /* ???:????0?,??1? */
    if(level != 0U)
    {
        HAL_GPIO_WritePin(GPIOF, pin, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(GPIOF, pin, GPIO_PIN_SET);
    }
}

void BSP_Led_Toggle(uint16_t pin)
{
    HAL_GPIO_TogglePin(GPIOF, pin);
}

void BSP_Beep_Set(uint8_t level)
{
    if(level != 0U)
    {
        HAL_GPIO_WritePin(BEEP_PORT, BEEP_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(BEEP_PORT, BEEP_PIN, GPIO_PIN_RESET);
    }
}

void BSP_Beep_Toggle(void)
{
    HAL_GPIO_TogglePin(BEEP_PORT, BEEP_PIN);
}

uint8_t BSP_Key_Read(void)
{
    if(HAL_GPIO_ReadPin(KEY_USER_PORT, KEY_USER_PIN) == GPIO_PIN_RESET)
    {
        return 1U;
    }
    return 0U;
}

