#ifndef __APP_SYS_INFO_H
#define __APP_SYS_INFO_H

#include <stdint.h>

/**
 * @brief Capture reset source + increment non-volatile boot counter.
 *        Call once at system startup (after FreeRTOS scheduler is up).
 *        Reads RCC->CSR before clearing reset flags; stores boot count in
 *        RTC backup register (survives soft/IWDG reset, needs VBAT for power loss).
 */
void AppSysInfo_Init(void);

/** @brief Reset reason string: "IWDG"/"WWDG"/"SOFT"/"POR"/"PIN"/"UNKNOWN". */
const char* AppSysInfo_GetReasonStr(void);

/** @brief Total boot count (incremented every startup). */
uint32_t AppSysInfo_GetBootCount(void);

/** @brief System uptime in seconds since boot. */
uint32_t AppSysInfo_GetUptimeSec(void);

#endif
