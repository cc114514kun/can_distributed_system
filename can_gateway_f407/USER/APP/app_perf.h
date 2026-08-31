#ifndef __APP_PERF_H
#define __APP_PERF_H

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* FreeRTOS run-time stats use the kernel tick count as the time base
 * (see portGET_RUN_TIME_COUNTER_VALUE in FreeRTOSConfig.h). 1 ms resolution
 * is sufficient for a monitoring dashboard and avoids a dedicated HW timer. */

/* Call periodically from any task; internally throttled to 1 Hz. Updates
 * rate windows (CAN RX/TX, UART TX bytes), queue peak usage and heap peak. */
void AppPerf_Update(void);

/* Accumulate UART TX byte counter (called from bsp_uart_send, task ctx). */
void AppPerf_UartTxBytes(uint16_t n);

/* Emit a multi-line "OK PERF ..." report through App_ReportPrint. */
void AppPerf_Report(void);

#endif
