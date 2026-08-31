#include "bsp_iwdg.h"

/* CubeMX?main.c???IWDG??,????,BSP???? */
extern IWDG_HandleTypeDef hiwdg;

void BSP_IWDG_Start(void)
{
    __HAL_IWDG_START(&hiwdg);
}

void BSP_IWDG_Feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
