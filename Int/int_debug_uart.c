#include "int_debug_uart.h"
#include "gd32f10x.h"
#include <stddef.h>
/* 初始化调试串口：PA9发送，PA10接收，115200波特率。 */
void debug_uart_init(void)
{
    /* 打开GPIO和串口外设的时钟。 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    /* PA9由串口外设控制输出，PA10用于接收。 */
    gpio_init(GPIOA, GPIO_MODE_AF_PP,
              GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING,
              GPIO_OSPEED_50MHZ, GPIO_PIN_10);

    /* 先复位串口配置，再设置通信参数。 */
    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);

    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);

    usart_enable(USART0);
}

/*
 * 发送一个字节，仅供主循环调用。
 * 返回1：数据已交给串口；返回0：等待超时。
 */
uint8_t debug_uart_send(uint8_t data)
{
    uint32_t waitCount = 0U;

    /* 等待发送数据寄存器空闲，限制等待次数以免卡死。 */
    while (RESET == usart_flag_get(USART0, USART_FLAG_TBE)) {
        waitCount++;
        if (waitCount >= 100000U) {
            return 0U;
        }
    }

    usart_data_transmit(USART0, data);
    return 1U;
}

/*
 * 主循环调用：按顺序发送指定长度的文本。
 * 返回1表示全部提交成功，返回0表示参数错误或发送超时。
 * 失败时可能已有部分字符发出；函数不会保存text指针。
 */
uint8_t debug_uart_write(const char *text, uint32_t length)
{
    if (text == NULL) {
        return 0U;
    }

    /* 复用单字节发送函数，任一字节失败就停止。 */
    for (uint32_t index = 0U; index < length; index++) {
        if (debug_uart_send((uint8_t)text[index]) == 0U) {
            return 0U;
        }
    }

    return 1U;
}

/*
 * 2026-11-16：为串口回显测试增加单字节接收。
 * 主循环调用，不等待数据；data不可为空，函数不会保存该指针。
 * 返回1表示已读取一个字节；返回0表示无数据或参数无效。
 */
uint8_t debug_uart_recv(uint8_t *data)
{
    if (data == NULL) {
        return 0U;
    }

    /* 仅在硬件收到数据后读取，读取数据寄存器会清除RBNE标志。 */
    if (RESET == usart_flag_get(USART0, USART_FLAG_RBNE)) {
        return 0U;
    }

    *data = (uint8_t)usart_data_receive(USART0);
    return 1U;
}
