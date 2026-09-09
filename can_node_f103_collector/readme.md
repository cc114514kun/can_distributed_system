# STM32F103ZET6 Multi‑Signal Acquisition Terminal

Hardware:正点原子精英开发板

## Function
1. ADC+DMA multi‑channel sampling, NTC temperature and potentiometer, sliding‑average filter
2. CAN communication: interrupt receive, Bus‑Off auto‑recovery, fault alarm report
3. RTC real‑time clock, Independent Watchdog(IWDG)
4. FreeRTOS software architecture, separated `bsp_*` driver layer and `app_*` business layer

## Environment
- STM32CubeMX
- Keil MDK‑ARM5
- FreeRTOS