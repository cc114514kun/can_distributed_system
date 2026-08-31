#include "app_config.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* RAM mirror of system config */
SystemConfig_t g_sys_cfg;

/**
 * @brief CRC32 (IEEE 802.3, reflected) over the config payload (excludes crc field).
 */
static uint32_t app_config_calc_crc(const uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for(uint32_t i = 0U; i < len; i++)
    {
        crc ^= buf[i];
        for(uint8_t b = 0U; b < 8U; b++)
        {
            if(crc & 1U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return ~crc;
}

/**
 * @brief Load factory defaults into g_sys_cfg (RAM only, not flash).
 */
void AppConfig_LoadDefault(void)
{
    memset(&g_sys_cfg, 0, sizeof(SystemConfig_t));

    g_sys_cfg.magic             = SYS_CONFIG_MAGIC;
    g_sys_cfg.version           = SYS_CONFIG_VERSION;
    g_sys_cfg.node_timeout_ms   = 500U;        /* node offline timeout */
    g_sys_cfg.temp_high_limit   = 8000;        /* Q100: 80.0C */
    g_sys_cfg.report_period_ms  = 500U;        /* node monitor scan period */
    g_sys_cfg.node_enable_mask  = 0x0FU;       /* MAX_NODE_NUM=4, bit0-bit3 enabled */
    g_sys_cfg.push_enable       = 0U;          /* active push OFF by default */
    g_sys_cfg.push_period_ms    = 1000U;       /* push period 1s when enabled */
}

/**
 * @brief Load config from flash if valid (magic/version/crc), else defaults.
 */
void AppConfig_Load(void)
{
    SystemConfig_t *p_flash_cfg = (SystemConfig_t *)SYS_CONFIG_FLASH_ADDR;

    /* magic check */
    if(p_flash_cfg->magic != SYS_CONFIG_MAGIC)
    {
        AppConfig_LoadDefault();
        return;
    }
    /* version check */
    if(p_flash_cfg->version != SYS_CONFIG_VERSION)
    {
        AppConfig_LoadDefault();
        return;
    }

    /* CRC check: over all bytes before crc field */
    const uint32_t calc_len = offsetof(SystemConfig_t, crc);
    uint32_t calc_crc = app_config_calc_crc((const uint8_t *)p_flash_cfg, calc_len);
    if(calc_crc != p_flash_cfg->crc)
    {
        AppConfig_LoadDefault();
        return;
    }

    /* valid: copy to RAM */
    memcpy(&g_sys_cfg, p_flash_cfg, sizeof(SystemConfig_t));
}

/**
 * @brief Save g_sys_cfg (RAM) to flash Sector11; fills magic/version/crc first.
 */
bool AppConfig_SaveToFlash(void)
{
    HAL_StatusTypeDef hal_ret;
    FLASH_EraseInitTypeDef erase_cfg;
    uint32_t err_sector = 0U;

    /* 1. compute crc over payload */
    const uint32_t calc_len = offsetof(SystemConfig_t, crc);
    g_sys_cfg.crc = app_config_calc_crc((const uint8_t *)&g_sys_cfg, calc_len);

    /* 2. unlock flash */
    HAL_FLASH_Unlock();

    /* 3. erase Sector11 */
    erase_cfg.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_cfg.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_cfg.Sector = FLASH_SECTOR_11;
    erase_cfg.NbSectors = 1U;
    hal_ret = HAL_FLASHEx_Erase(&erase_cfg, &err_sector);
    if(hal_ret != HAL_OK)
    {
        HAL_FLASH_Lock();
        return false;
    }

    /* 4. program the struct (F4 is 32-bit word aligned) */
    uint32_t *p_src = (uint32_t *)&g_sys_cfg;
    uint32_t *p_dst = (uint32_t *)SYS_CONFIG_FLASH_ADDR;
    for(uint32_t i = 0U; i < sizeof(SystemConfig_t)/4U; i++)
    {
        hal_ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
            (uint32_t)(p_dst + i), p_src[i]);
        if(hal_ret != HAL_OK)
        {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();
    return true;
}
