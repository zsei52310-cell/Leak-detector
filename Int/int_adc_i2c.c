#include "int_adc_i2c.h"
#include "gd32f10x.h"
#include <stddef.h>

#define I2C_SCL GPIO_PIN_8
#define I2C_SDA GPIO_PIN_9
static uint8_t g_ready = 0U;
static uint32_t g_cyclesPerUs = 0U;

/* 2026-11-16：DWT计数延时，不占用SysTick或蜂鸣器定时器。
 * 仅在主循环使用；若计数器停止，有限次数后将总线标为不可用。 */
static void adc_i2c_delay(uint32_t delayUs)
{
    uint32_t cycles = g_cyclesPerUs * delayUs;
    uint32_t start = DWT->CYCCNT;
    uint32_t spins = 0U;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
        if (++spins >= (cycles + 1000U)) {
            g_ready = 0U;
            return;
        }
    }
}

/* 2026-11-16：释放SCL并检查真实电平，最多等待约1ms。 */
static uint8_t adc_i2c_raise(void)
{
    gpio_bit_set(GPIOB, I2C_SCL);
    for (uint32_t count = 0U; count < 200U; count++) {
        if (g_ready == 0U) {
            return 0U;
        }
        if (SET == gpio_input_bit_get(GPIOB, I2C_SCL)) {
            adc_i2c_delay(5U);
            return g_ready;
        }
        adc_i2c_delay(5U);
    }
    return 0U;
}

/* 2026-11-16：生成STOP；SCL被外部拉低时仍释放两根线。 */
static uint8_t adc_i2c_stop(void)
{
    uint8_t status;
    gpio_bit_reset(GPIOB, I2C_SCL | I2C_SDA);
    adc_i2c_delay(5U);
    status = adc_i2c_raise();
    gpio_bit_set(GPIOB, I2C_SDA);
    adc_i2c_delay(5U);
    return (uint8_t)((status != 0U) && (g_ready != 0U) &&
                    (SET == gpio_input_bit_get(GPIOB, I2C_SDA)));
}

/* 2026-11-16：发送8位并读取从机ACK，不假定屏幕一定在线。 */
static uint8_t adc_i2c_byte(uint8_t value)
{
    uint8_t ack;
    for (uint32_t bit = 0U; bit < 8U; bit++) {
        gpio_bit_reset(GPIOB, I2C_SCL);
        gpio_bit_write(GPIOB, I2C_SDA, (value & 0x80U) ? SET : RESET);
        adc_i2c_delay(5U);
        if (adc_i2c_raise() == 0U) {
            return 0U;
        }
        gpio_bit_reset(GPIOB, I2C_SCL);
        value = (uint8_t)(value << 1U);
    }
    /* 第9个时钟由从机驱动SDA，主机必须释放SDA。 */
    gpio_bit_set(GPIOB, I2C_SDA);
    adc_i2c_delay(5U);
    if (adc_i2c_raise() == 0U) {
        return 0U;
    }
    ack = (uint8_t)(RESET == gpio_input_bit_get(GPIOB, I2C_SDA));
    gpio_bit_reset(GPIOB, I2C_SCL);
    adc_i2c_delay(5U);
    return (uint8_t)((ack != 0U) && (g_ready != 0U));
}

/* 2026-11-16：初始化ADC独立总线，等待模块上电并尝试释放残留事务。 */
uint8_t adc_i2c_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    g_cyclesPerUs = rcu_clock_freq_get(CK_AHB) / 1000000U;
    if (g_cyclesPerUs == 0U) {
        return 0U;
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_ready = 1U;
    /* 写1仅释放开漏引脚，绝不推挽输出高电平。 */
    gpio_bit_set(GPIOB, I2C_SCL | I2C_SDA);
    gpio_init(GPIOB, GPIO_MODE_OUT_OD, GPIO_OSPEED_2MHZ, I2C_SCL | I2C_SDA);
    for (uint32_t count = 0U; count < 100U; count++) {
        adc_i2c_delay(1000U);
    }
    for (uint32_t count = 0U; count < 9U; count++) {
        if (SET == gpio_input_bit_get(GPIOB, I2C_SDA)) {
            break;
        }
        gpio_bit_reset(GPIOB, I2C_SCL);
        adc_i2c_delay(5U);
        if (adc_i2c_raise() == 0U) {
            gpio_bit_set(GPIOB, I2C_SCL | I2C_SDA);
            return 0U;
        }
    }
    return adc_i2c_stop();
}

/* 2026-11-16：ADC专用主机写事务，失败不自动重发半个命令序列。 */
static uint8_t adc_i2c_write(uint8_t address, const uint8_t *data, size_t length)
{
    uint8_t status = 1U;
    if ((g_ready == 0U) || (address > 0x7FU) ||
        ((data == NULL) && (length != 0U)) || (length > 129U)) {
        return 0U;
    }
    gpio_bit_set(GPIOB, I2C_SDA);
    if ((adc_i2c_raise() == 0U) ||
        (RESET == gpio_input_bit_get(GPIOB, I2C_SDA))) {
        gpio_bit_set(GPIOB, I2C_SCL | I2C_SDA);
        return 0U;
    }
    /* SCL为高时拉低SDA生成START，再在低电平期间准备数据。 */
    gpio_bit_reset(GPIOB, I2C_SDA);
    adc_i2c_delay(5U);
    gpio_bit_reset(GPIOB, I2C_SCL);
    status = adc_i2c_byte((uint8_t)(address << 1U));
    for (size_t index = 0U; (index < length) && (status != 0U); index++) {
        status = adc_i2c_byte(data[index]);
    }
    if (adc_i2c_stop() == 0U) {
        status = 0U;
    }
    return status;
}

/* 2026-11-16：写一个16位寄存器，MSB在先，逐字节检查ACK。 */
uint8_t adc_i2c_write16(uint8_t address, uint8_t reg, uint16_t value)
{
    uint8_t data[] = {reg, (uint8_t)(value >> 8U), (uint8_t)value};
    if (reg > 6U) {
        return 0U;
    }
    return adc_i2c_write(address, data, sizeof(data));
}

/* 2026-11-16：读取一字节；前一字节ACK，最后一字节NACK。
 * data不可为空且只在成功后更新，由read16提供局部存储。 */
static uint8_t adc_i2c_recv(uint8_t sendAck, uint8_t *data)
{
    uint8_t result = 0U;
    gpio_bit_set(GPIOB, I2C_SDA);
    for (uint32_t bit = 0U; bit < 8U; bit++) {
        adc_i2c_delay(5U);
        if (adc_i2c_raise() == 0U) {
            return 0U;
        }
        result = (uint8_t)(result << 1U);
        if (SET == gpio_input_bit_get(GPIOB, I2C_SDA)) {
            result |= 1U;
        }
        gpio_bit_reset(GPIOB, I2C_SCL);
    }
    gpio_bit_write(GPIOB, I2C_SDA, sendAck ? RESET : SET);
    adc_i2c_delay(5U);
    if (adc_i2c_raise() == 0U) {
        return 0U;
    }
    gpio_bit_reset(GPIOB, I2C_SCL);
    gpio_bit_set(GPIOB, I2C_SDA);
    adc_i2c_delay(5U);
    if (g_ready == 0U) {
        return 0U;
    }
    *data = result;
    return 1U;
}

/* 2026-11-16：写寄存器指针、重复START、读MSB/LSB，最后STOP。
 * 任一错误都不更新value，避免调用者误用半个读数。 */
uint8_t adc_i2c_read16(uint8_t address, uint8_t reg, uint16_t *value)
{
    uint8_t highByte = 0U;
    uint8_t lowByte = 0U;
    uint8_t status;
    if ((g_ready == 0U) || (value == NULL) ||
        (address > 0x7FU) || (reg > 6U)) {
        return 0U;
    }
    gpio_bit_set(GPIOB, I2C_SDA);
    if ((adc_i2c_raise() == 0U) ||
        (RESET == gpio_input_bit_get(GPIOB, I2C_SDA))) {
        gpio_bit_set(GPIOB, I2C_SCL | I2C_SDA);
        return 0U;
    }
    gpio_bit_reset(GPIOB, I2C_SDA);
    adc_i2c_delay(5U);
    gpio_bit_reset(GPIOB, I2C_SCL);
    status = adc_i2c_byte((uint8_t)(address << 1U));
    if (status != 0U) {
        status = adc_i2c_byte(reg);
    }
    if (status != 0U) {
        /* SCL低时释放SDA，然后在SCL高时再次拉低SDA。 */
        gpio_bit_set(GPIOB, I2C_SDA);
        adc_i2c_delay(5U);
        status = adc_i2c_raise();
        if (status != 0U) {
            gpio_bit_reset(GPIOB, I2C_SDA);
            adc_i2c_delay(5U);
            gpio_bit_reset(GPIOB, I2C_SCL);
            status = adc_i2c_byte((uint8_t)(((uint32_t)address << 1U) | 1U));
        }
    }
    if (status != 0U) {
        status = adc_i2c_recv(1U, &highByte);
    }
    if (status != 0U) {
        status = adc_i2c_recv(0U, &lowByte);
    }
    if (adc_i2c_stop() == 0U) {
        status = 0U;
    }
    if (status != 0U) {
        *value = (uint16_t)(((uint16_t)highByte << 8U) | lowByte);
    }
    return status;
}

/* 2026-11-16：诊断阶段使用的有限等待，后续调度层可替换为时间戳。 */
uint8_t adc_i2c_wait(uint32_t durationMs)
{
    if ((g_ready == 0U) || (durationMs == 0U) || (durationMs > 1000U)) {
        return 0U;
    }
    for (uint32_t count = 0U; count < durationMs; count++) {
        adc_i2c_delay(1000U);
        if (g_ready == 0U) {
            return 0U;
        }
    }
    return 1U;
}

/* 2026-11-16：诊断地址ACK，STOP之后释放总线。 */
uint8_t adc_i2c_probe(uint8_t address)
{
    return adc_i2c_write(address, NULL, 0U);
}
