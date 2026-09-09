#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "stm32f1xx_hal.h"

#define ADC_CH_NUM        2U
#define ADC_FILTER_WIN    8U
#define MEDIAN_SAMPLE_CNT 5U    //中值滤波采样点数

enum ADC_INDEX
{
    ADC_IDX_POT = 0, //电位器通道
    ADC_IDX_NTC = 1, //NTC通道
};

//-------------------对外API-------------------
void ADC_DMA_Init(void);
uint16_t ADC_Get_Filter_Value(uint8_t idx);
float ADC_Get_Voltage(uint16_t adc_val);
void BSP_ADC_GetFilterValue(uint16_t *val_pot, uint16_t *val_ntc);
float BSP_NTC_CalcTemp(uint16_t adc_raw);

#endif
