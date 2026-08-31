#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* Flash storage: F407 Sector11 (last sector of 1MB flash). */
#define SYS_CONFIG_FLASH_ADDR        ((uint32_t)0x080C0000U)
#define SYS_CONFIG_MAGIC             0xA5A51234U
#define SYS_CONFIG_VERSION           0x0101U

/* System configuration persisted in flash. */
typedef struct
{
    uint32_t magic;                 /* marker, fixed */
    uint16_t version;               /* struct version */
    uint32_t node_timeout_ms;       /* node offline timeout (ms) */
    int16_t  temp_high_limit;       /* temperature high-limit, Q100 (80.0C = 8000) */
    uint16_t report_period_ms;      /* node monitor scan period (ms) */
    uint32_t node_enable_mask;      /* node enable mask: bit0=node1 ... */
    uint8_t  push_enable;           /* active push enable: 0=off 1=on */
    uint32_t push_period_ms;        /* active push period (ms) */
    uint32_t crc;                   /* CRC32 over all bytes before crc */
} SystemConfig_t;

extern SystemConfig_t g_sys_cfg;

void AppConfig_Load(void);
bool AppConfig_SaveToFlash(void);
void AppConfig_LoadDefault(void);

#endif
