#ifndef INT_DEBUG_UART_H
#define INT_DEBUG_UART_H

#include <stdint.h>

void debug_uart_init(void);
uint8_t debug_uart_send(uint8_t data);
uint8_t debug_uart_write(const char *text, uint32_t length);
/* 非阻塞接收：返回1时，data指向的字节有效；返回0时不修改输出。 */
uint8_t debug_uart_recv(uint8_t *data);

#endif
