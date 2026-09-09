#include "bsp_rtc.h"
#include "main.h"
#include "stm32f1xx_hal_rtc.h"

extern RTC_HandleTypeDef hrtc;
//定义BKP_DR0寄存器，F1备份寄存器0（存初始化魔数）
#define BKP_DR0_REG   (*(__IO uint16_t *)(BKP_BASE + 0x04U))
//BKP_DR1：存放RTC"基准日期"(年月日)，用于复位后还原日期锚点
#define BKP_DR1_REG   (*(__IO uint16_t *)(BKP_BASE + 0x08U))
//初始化魔数：故意与原0x5A5A不同，确保升级后触发一次时间初始化
#define RTC_INIT_MAGIC   0x55AAU
//全局闹钟标志，中断写，主循环读取，必须加volatile
volatile uint8_t g_alarm_flag = 0U;

//把"编译时刻"(__DATE__ __TIME__)解析进RTC_DateTime_t，成功返回0
static uint8_t ParseBuildTime(RTC_DateTime_t *dt)
{
    static const char *mon_names[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec"};
    const char *date = __DATE__;   //例如 "Aug 17 2026"
    const char *time = __TIME__;   //例如 "11:07:44"

    uint8_t mo = 0U;
    for (uint8_t i = 0U; i < 12U; i++)
    {
        if (date[0]==mon_names[i][0] && date[1]==mon_names[i][1] && date[2]==mon_names[i][2])
        { mo = (uint8_t)(i + 1U); break; }
    }
    if (mo == 0U) return 1U;

    const char *p = date + 3;
    while (*p == ' ') p++;
    uint8_t day = 0U;
    while (*p >= '0' && *p <= '9') { day = (uint8_t)(day*10U + (uint8_t)(*p-'0')); p++; }
    while (*p == ' ') p++;
    uint16_t yr = 0U;
    while (*p >= '0' && *p <= '9') { yr = (uint16_t)(yr*10U + (uint16_t)(*p-'0')); p++; }

    dt->year  = yr;
    dt->month = mo;
    dt->date  = day;
    dt->week  = 0U;
    dt->hour  = (uint8_t)((time[0]-'0')*10 + (time[1]-'0'));
    dt->min   = (uint8_t)((time[3]-'0')*10 + (time[4]-'0'));
    dt->sec   = (uint8_t)((time[6]-'0')*10 + (time[7]-'0'));
    return 0U;
}

//在 dt 日期上增加 days 天（处理跨月/跨年/闰年）
static void AddDaysToDate(RTC_DateTime_t *dt, uint32_t days)
{
    static const uint8_t mdays[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    while (days--)
    {
        uint8_t max = mdays[dt->month];
        if (dt->month == 2U &&
            ((dt->year % 4U == 0U && dt->year % 100U != 0U) || dt->year % 400U == 0U))
            max = 29U;
        if (++dt->date > max)
        {
            dt->date = 1U;
            if (++dt->month > 12U)
            {
                dt->month = 1U;
                dt->year++;
            }
        }
    }
}

/**
 * @brief RTC初始化
 * F1的日期(年月日)只存在RAM的DateToUpdate里，HAL每次上电都重置为2000-01-01，
 * 且不会写入备份寄存器；而秒计数器(RTC_CNT)在备份域掉电才清零。
 * 因此：冷上电用"编译时刻"初始化并把基准日期存备份域；热复位再据计数器还原日期。
 */
void BSP_RTC_Init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();     //开启PWR电源时钟
    __HAL_RCC_BKP_CLK_ENABLE();     //开启BKP备份域时钟，F1必须开启
    HAL_PWR_EnableBkUpAccess();     //解除备份域写保护

    if (BKP_DR0_REG != RTC_INIT_MAGIC)
    {
        // 首次上电：用固件编译时刻初始化RTC，并把基准日期存进备份寄存器
        RTC_DateTime_t dt = {2024,1,1,0,0,0,0}; //解析失败兜底，避免回到2000
        if (ParseBuildTime(&dt) == 0U)
        {
            BSP_RTC_SetDateTime(&dt);
        }
        // 打包基准日期(年偏移7bit | 月4bit | 日5bit)写备份域
        uint16_t base = (uint16_t)(((dt.year - 2000U) << 9) | ((uint16_t)dt.month << 5) | dt.date);
        BKP_DR1_REG = base;
        BKP_DR0_REG = RTC_INIT_MAGIC;
    }
    else
    {
        // 复位/热启动：RTC秒计数器仍在走，但RAM日期锚点被HAL重置为2000，需还原
        uint16_t base = BKP_DR1_REG;
        RTC_DateTime_t dt = {0};
        dt.year  = (uint16_t)(((base >> 9) & 0x7FU) + 2000U);
        dt.month = (uint8_t)((base >> 5) & 0x0FU);
        dt.date  = (uint8_t)(base & 0x1FU);

        // 按RTC已走秒数推算经过天数，加到基准日期上
        uint32_t counter = (((uint32_t)hrtc.Instance->CNTH & 0xFFFFU) << 16U)
                          | ((uint32_t)hrtc.Instance->CNTL & 0xFFFFU);
        AddDaysToDate(&dt, counter / 86400U);

        // 只恢复日期锚点（HAL_RTC_SetDate 会把计数器收敛到当天，不影响秒计时）
        RTC_DateTypeDef sDate = {0};
        sDate.Year    = (uint8_t)(dt.year - 2000U);
        sDate.Month   = dt.month;
        sDate.Date    = dt.date;
        sDate.WeekDay = 0U; //HAL_RTC_SetDate内部会重算星期
        HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    }
}

//设置时间
void BSP_RTC_SetDateTime(RTC_DateTime_t *dt)
{
    if(dt == NULL) return;

    // 设置日期
    RTC_DateTypeDef sDate={0};
    RTC_TimeTypeDef sTime={0};

    
    sDate.Year = dt->year - 2000;  //年份2000-2099
    sDate.Month = dt->month;      //月份1-12
    sDate.Date = dt->date;         //日期1-31
    sDate.WeekDay = dt->week;     //星期0-6
    if(HAL_RTC_SetDate(&hrtc,&sDate,RTC_FORMAT_BIN) != HAL_OK) return;


    sTime.Hours = dt->hour;  //小时0-23
    sTime.Minutes = dt->min;  //分钟0-59
    sTime.Seconds = dt->sec;  //秒0-59
    if(HAL_RTC_SetTime(&hrtc,&sTime,RTC_FORMAT_BIN) != HAL_OK) return;

    if(HAL_RTC_WaitForSynchro(&hrtc) != HAL_OK)
    {
        Error_Handler();
    }
    //✅F1标准函数关闭闹钟A
    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);

}

//读取当前时间
/**
 * @brief 读取RTC时间，❗F1必须先GetTime再GetDate
 * @param dt 输出时间结构体
 */
void BSP_RTC_GetDateTime(RTC_DateTime_t *dt)
{
    if(dt == NULL) return;

    RTC_DateTypeDef sDate;
    RTC_TimeTypeDef sTime;

    HAL_RTC_GetTime(&hrtc,&sTime,RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc,&sDate,RTC_FORMAT_BIN);

    dt->year  = 2000U + sDate.Year;
    dt->month = sDate.Month;
    dt->date  = sDate.Date;
    dt->week  = sDate.WeekDay;
    dt->hour  = sTime.Hours;
    dt->min   = sTime.Minutes;
    dt->sec   = sTime.Seconds;
}

//设置闹钟时间
/**
 * @brief 设置闹钟A
 * @param hour 时 min分 sec秒
 */
void BSP_RTC_SetAlarmA(uint8_t hour,uint8_t min,uint8_t sec)
{
    RTC_AlarmTypeDef sAlarm={0};
    //F1标准函数关闭闹钟A
    HAL_RTC_DeactivateAlarm(&hrtc,RTC_ALARM_A);


    sAlarm.AlarmTime.Hours = hour;
    sAlarm.AlarmTime.Minutes = min;
    sAlarm.AlarmTime.Seconds = sec;
    sAlarm.Alarm = RTC_ALARM_A;
    if(HAL_RTC_SetAlarm_IT(&hrtc,&sAlarm,RTC_FORMAT_BIN) != HAL_OK)
    {
        Error_Handler();
    }
    //清除中断标志
    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc,RTC_FLAG_ALRAF);
}

//RTC闹钟中断回调
/**
 * @brief 闹钟A中断回调，只置标志，禁止printf、延时
 */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    g_alarm_flag = 1;
}

/**
* @brief RTC间隔闹钟：sec秒之后触发闹钟A，完整支持日期进位、跨天
 * @param sec 间隔秒数 1 ~ 若干天
 */
void BSP_RTC_SetAlarmAfterSec(uint32_t sec)
{
    RTC_DateTime_t t;
    BSP_RTC_GetDateTime(&t);

    // ========== 1、把全部时间换算成总秒数 ==========
    uint32_t total_sec = t.sec;
    total_sec += (uint32_t)t.min * 60U;
    total_sec += (uint32_t)t.hour * 3600U;

    //加上间隔秒数
    total_sec += sec;
    
    // ========== 2、把总秒数换算成时间 ==========
    const uint32_t day_sec = 24U * 60U * 60U;
    uint32_t add_days = total_sec / day_sec; //需要加上的天数
    uint32_t remain_sec = total_sec % day_sec; //剩余秒数

    //把剩余秒数转回 时:分:秒
    t.hour = remain_sec / 3600U;
    remain_sec %= 3600U;
    t.min  = remain_sec / 60U;
    t.sec  = remain_sec % 60U;

    // ========== 3、日期增加 add_days天，处理月份、闰年 ==========
    //每个月天数，索引0不用，1‑12月
    const uint8_t month_days[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

    //日期增加处理
    while(add_days > 0U)
    {
        uint8_t month_max_day; //当月最大天数

        //闰年判断：能被4整除，不是世纪年；F1 RTC年份0‑99（2000‑2099）
        if( (t.year % 4U == 0U) && (t.month == 2U) )
        {
            month_max_day = 29U;
        }
        else
        {
            month_max_day = month_days[t.month];
        }

        t.date += 1U; //日期增加1天
        add_days -=1U; //需要加上的天数减少1天

        //当月日期溢出，进月份
        if(t.date > month_max_day)
        {
            t.date = 1U;
            t.month +=1U;
            //月份溢出，进年份
            if(t.month >12U)
            {
                t.month = 1U;
                t.year +=1U;
            }
        }
    }
    // ==========4、设置闹钟到计算完成的时刻 ==========
    BSP_RTC_SetAlarmA(t.hour, t.min, t.sec);
}
