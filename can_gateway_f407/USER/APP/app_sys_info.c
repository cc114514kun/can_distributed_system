#include "app_sys_info.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static char     g_reset_reason[16] = "UNKNOWN";
static uint32_t g_boot_count = 0U;

void AppSysInfo_Init(void)
{
    /* 1. Read reset source from RCC->CSR BEFORE clearing the flags. */
    uint32_t csr = RCC->CSR;
    if(csr & RCC_CSR_IWDGRSTF)
    {
        strcpy(g_reset_reason, "IWDG");
    }
    else if(csr & RCC_CSR_WWDGRSTF)
    {
        strcpy(g_reset_reason, "WWDG");
    }
    else if(csr & RCC_CSR_SFTRSTF)
    {
        strcpy(g_reset_reason, "SOFT");
    }
    else if(csr & RCC_CSR_PORRSTF)
    {
        strcpy(g_reset_reason, "POR");
    }
    else if(csr & RCC_CSR_PINRSTF)
    {
        strcpy(g_reset_reason, "PIN");
    }
    else
    {
        strcpy(g_reset_reason, "UNKNOWN");
    }

    /* 2. Increment non-volatile boot counter in RTC backup register.
     *    Requires PWR clock + backup-domain access unlock. */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    uint32_t bc = READ_REG(RTC->BKP0R);
    g_boot_count = bc + 1U;
    WRITE_REG(RTC->BKP0R, g_boot_count);

    /* 3. Clear reset flags so the next boot reads a fresh source. */
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

const char* AppSysInfo_GetReasonStr(void)
{
    return g_reset_reason;
}

uint32_t AppSysInfo_GetBootCount(void)
{
    return g_boot_count;
}

uint32_t AppSysInfo_GetUptimeSec(void)
{
    return (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
}
