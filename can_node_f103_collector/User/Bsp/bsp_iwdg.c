#include "bsp_iwdg.h"

extern IWDG_HandleTypeDef hiwdg;

void BSP_IWDG_Init(void)
{
    HAL_IWDG_Init(&hiwdg);
}

//喂狗，必须在超时时间内调用
void BSP_IWDG_Feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
