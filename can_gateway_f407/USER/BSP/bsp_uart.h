#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#define UART_RX_RING_BUF_LEN  512U

typedef struct
{
    uint8_t buf[UART_RX_RING_BUF_LEN];
    uint16_t rd_idx;
    uint16_t wr_idx;
}UartRingBuf_t;

void bsp_uart_init(UART_HandleTypeDef *huart);
/* 读取一行，遇到\r\n返回true，输出line不带换行 */
bool bsp_uart_read_line(uint8_t *line_buf, uint16_t buf_size, uint16_t *out_len);
/* 底层发送 */
void bsp_uart_send(const uint8_t *data, uint16_t len);
/* 供 Core/Src/stm32f4xx_it.c 的 USART1_IRQHandler 调用 */
void bsp_uart_rx_isr(void);

#endif

