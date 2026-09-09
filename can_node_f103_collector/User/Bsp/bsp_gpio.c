#include "bsp_gpio.h"

//BSP GPIO初始化：LED（PB5/PE5）推挽输出，按键（PA4/PA5）上拉输入
void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    //使能时钟（GPIOB在正点原子精英版必开，GPIOE在大容量ZET6有）
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    //LED0 -> PB5 推挽输出，默认熄灭（高电平）
    GPIO_InitStruct.Pin   = GPIO_PIN_5;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);

    //LED1 -> PE5 推挽输出，默认熄灭（高电平）
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);

    //KEY0 -> PA4 上拉输入（按键另一端接GND）
    GPIO_InitStruct.Pin  = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    //KEY1 -> PA5 上拉输入
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

//读取两路开关状态
uint8_t BSP_GPIO_ReadKey(uint8_t key_id)
{
    uint8_t ret = 0;
    if(key_id == KEY_0)
    {
        //内部上拉，接通GND为低电平，返回1代表开关按下
        if(HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_4) == GPIO_PIN_RESET)
            ret = 1;
        else
            ret = 0;
    }
    else if(key_id == KEY_1)
    {
        if(HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_5) == GPIO_PIN_RESET)
            ret =1;
        else
            ret =0;
    }
    return ret;
}

//控制LED：LED0=PB5，LED1=PE5；state=1亮（低电平），0灭（高电平）
void BSP_GPIO_SetLed(uint8_t led_id,uint8_t state)
{
    if(led_id == LED_0)
    {
        if(state) HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,GPIO_PIN_RESET);
        else      HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,GPIO_PIN_SET);
    }
    else if(led_id == LED_1)
    {
        if(state) HAL_GPIO_WritePin(GPIOE,GPIO_PIN_5,GPIO_PIN_RESET);
        else      HAL_GPIO_WritePin(GPIOE,GPIO_PIN_5,GPIO_PIN_SET);
    }
}
