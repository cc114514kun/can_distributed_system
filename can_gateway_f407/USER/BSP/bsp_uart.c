#include "bsp_uart.h"
#include "stm32f4xx_hal.h"
#include "app_perf.h"

extern UART_HandleTypeDef huart1;
static UartRingBuf_t g_uart_rx_ring;

void bsp_uart_init(UART_HandleTypeDef *huart)
{
    (void)huart;

    g_uart_rx_ring.rd_idx = 0U;
    g_uart_rx_ring.wr_idx = 0U;

    /* USART1 RX is handled directly in the IRQ handler below, bypassing
       HAL_UART_Receive_IT() entirely. This avoids two failure modes we hit:
       (1) HAL_UART_Transmit() taking the UART handle lock and starving RX
           interrupts while TX is in progress;
       (2) HAL_UART_Receive_IT() disabling itself on Overrun/Frame/Noise
           errors and never re-arming because we have no ErrorCallback.
       MX_USART1_UART_Init() only configures the peripheral clocks/pins/baud;
       it does NOT enable RXNE interrupts, so we must do it here. */
    SET_BIT(huart1.Instance->CR1, USART_CR1_RXNEIE);
    SET_BIT(huart1.Instance->CR3, USART_CR3_EIE);

    /* USART1 RX ISR only writes to a ring buffer and never calls FreeRTOS APIs,
       so it can safely use a higher urgency (lower numeric value) than the CAN
       interrupts and also sit above configMAX_SYSCALL_INTERRUPT_PRIORITY.
       Numeric priority 2 is NOT masked by FreeRTOS critical sections, which
       prevents any task-level critical region from delaying UART RX processing
       and causing Overrun Error. */
    HAL_NVIC_SetPriority(USART1_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* Called from Core/Src/stm32f4xx_it.c USART1_IRQHandler. */
void bsp_uart_rx_isr(void)
{
    uint32_t sr = huart1.Instance->SR;

    if(sr & USART_SR_RXNE)
    {
        /* RXNE set: a valid byte is waiting in DR. Reading DR returns that
           byte AND clears any coincident ORE/NE/FE latch (per RM0090 the DR
           read is the required error-clearing step). Store it and advance. */
        uint8_t ch = (uint8_t)(huart1.Instance->DR & 0xFFU);
        g_uart_rx_ring.buf[g_uart_rx_ring.wr_idx] = ch;
        g_uart_rx_ring.wr_idx = (g_uart_rx_ring.wr_idx + 1U) % UART_RX_RING_BUF_LEN;
    }
    else if(sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE))
    {
        /* No data byte pending, only an error latch (e.g. overrun with no fresh
           RXNE). Reading DR once clears the error so the receiver resumes. Using
           else-if (NOT a second read) avoids discarding a good following byte
           when RXNE was already handled above -- this was a source of dropped
           leading bytes in command lines (GET_FAULT received as ULT/ALT). */
        (void)huart1.Instance->DR;
    }
}

bool bsp_uart_read_line(uint8_t *line_buf, uint16_t buf_size, uint16_t *out_len)
{
    *out_len = 0U;
    if(line_buf == NULL || buf_size < 2U)
        return false;

    while(g_uart_rx_ring.rd_idx != g_uart_rx_ring.wr_idx)
    {
        uint8_t ch = g_uart_rx_ring.buf[g_uart_rx_ring.rd_idx];
        g_uart_rx_ring.rd_idx = (g_uart_rx_ring.rd_idx + 1U) % UART_RX_RING_BUF_LEN;

        if(ch == '\r' || ch == '\n')
        {
            if(*out_len > 0U)
            {
                line_buf[*out_len] = 0;
                return true;
            }
            continue;
        }
        if(*out_len < (buf_size-1U))
        {
            line_buf[(*out_len)++] = ch;
        }
    }
    return false;
}

void bsp_uart_send(const uint8_t *data, uint16_t len)
{
    if((len == 0U) || (data == NULL))
        return;

    /* Do NOT use HAL_UART_Transmit(). It takes the HAL UART handle lock
       (__HAL_LOCK) and starves RX interrupts during the whole transmit.
       Instead poll the hardware TXE/TC flags directly. This never touches
       huart->Lock, so RXNE keeps being serviced while TX is in progress.
       ReportTask is the sole caller, so no TX re-entrancy issue. */
    for(uint16_t i = 0U; i < len; i++)
    {
        uint32_t wait = 0U;
        while(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) == RESET)
        {
            if(++wait >= 100000U)
            {
                /* TXE stuck; abort this byte to avoid hanging the system */
                return;
            }
        }
        huart1.Instance->DR = data[i];
    }

    AppPerf_UartTxBytes(len);

    /* Wait for last byte to leave the shift register. */
    uint32_t wait = 0U;
    while(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET)
    {
        if(++wait >= 100000U)
        {
            return;
        }
    }
}

