#include "bsp_adc.h"
#include "math.h"

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;

//DMA原始缓冲区,外部禁止直接读取
static uint16_t adc_dma_buf[ADC_CH_NUM] = {0};
static uint16_t adc_buf_copy[ADC_CH_NUM] = {0};

//滑动平均缓存
static uint16_t filter_buf[ADC_CH_NUM][ADC_FILTER_WIN] = {0};  //滑动平均缓存
static uint8_t filter_index[ADC_CH_NUM] = {0}; //滑动平均索引

/**
 * @brief DMA传输完成中断回调，在这里拷贝副本，隔离DMA与CPU读写冲突
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if(hadc->Instance == ADC1) //只处理ADC1
    {
        //把DMA刚采集完成的数据拷贝到业务副本缓冲区
        for(uint8_t i = 0; i < ADC_CH_NUM; i++)
        {
            uint16_t raw = adc_dma_buf[i];
            //STM32 ADC 12bit，合法范围 0~4095
            if(raw <= 4095U)
            {
                adc_buf_copy[i] = raw;
            }
            //非法值：保留上一次合法值，不更新
        }
    }
}

/**
 * @brief 中值滤波：去除偶发尖峰、0值毛刺
 */
static uint16_t adc_median_filter(uint16_t *buf, uint8_t len)
{
    uint16_t temp[MEDIAN_SAMPLE_CNT];
    for(uint8_t i = 0; i < len; i++)
    {
        temp[i] = buf[i];
    }
    //简单冒泡排序
    for(uint8_t i = 0; i < len-1; i++)
    {
        for(uint8_t j = 0; j < len-i-1; j++)
        {
            if(temp[j] > temp[j+1])
            {
                uint16_t t = temp[j];
                temp[j] = temp[j+1];
                temp[j+1] = t;
            }
        }
    }
    return temp[len/2]; //返回中间值
}

/**
 * @brief 滑动平均滤波
 */
static uint16_t adc_average_filter(uint8_t idx, uint16_t new_val)
{
    filter_buf[idx][filter_index[idx]] = new_val; //更新缓存
    filter_index[idx] = (filter_index[idx] + 1) % ADC_FILTER_WIN; //更新索引

    uint32_t sum = 0;
    for(uint8_t i = 0; i < ADC_FILTER_WIN; i++)
    {
        sum += filter_buf[idx][i];
    }
    return (uint16_t)(sum / ADC_FILTER_WIN);
}

/**
 * @brief 获取经过【中值+滑动平均】二级滤波后的ADC值
 */
uint16_t ADC_Get_Filter_Value(uint8_t idx)
{
    if(idx >= ADC_CH_NUM) return 0U;

    static uint16_t median_buf[ADC_CH_NUM][MEDIAN_SAMPLE_CNT] = {0}; //中值滤波缓存
    static uint8_t med_idx[ADC_CH_NUM] = {0}; //中值滤波索引

    median_buf[idx][med_idx[idx]] = adc_buf_copy[idx]; //更新缓存
    med_idx[idx] = (med_idx[idx]+1) % MEDIAN_SAMPLE_CNT; //更新索引

    uint16_t med_out = adc_median_filter(median_buf[idx], MEDIAN_SAMPLE_CNT); //中值滤波
    uint16_t avg_out = adc_average_filter(idx, med_out); //滑动平均滤波

    return avg_out;
}

/**
 * @brief ADC原始值转电压
 */
float ADC_Get_Voltage(uint16_t adc_val)
{
    return adc_val * 3.3f / 4095.0f;
}

/**
 * @brief 启动ADC DMA循环采集，main只调用一次
 */
void ADC_DMA_Init(void)
{
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf, ADC_CH_NUM);
}

/* @brief 同时获取两路滤波结果
 * @param val_pot  输出：电位器(ADC_IDX_POT)滤波值
 * @param val_ntc  输出：NTC(ADC_IDX_NTC)滤波值
 */
void BSP_ADC_GetFilterValue(uint16_t *val_pot, uint16_t *val_ntc)
{
    *val_pot = ADC_Get_Filter_Value(ADC_IDX_POT);
    *val_ntc = ADC_Get_Filter_Value(ADC_IDX_NTC);
}
//NTC温度换算函数，10K分压，B=3950
float BSP_NTC_CalcTemp(uint16_t adc_raw)
{
    if(adc_raw == 0) return -99;

    float v_adc = adc_raw * 3.3f / 4095.0f;
    //硬件电路：3.3V‑NTC‑10K‑GND B=3950
    float r_ntc = (3.3f - v_adc) / v_adc * 10000.0f;

    const float B_VALUE = 3950.0f;
    float tempK = 1.0f / (1.0f/(273.15f + 25.0f) + logf(r_ntc / 10000.0f)/B_VALUE);
    return tempK - 273.15f;
}
