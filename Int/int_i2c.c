#include "int_i2c.h"
#include "gd32f10x.h"

#define I2C_SCL GPIO_PIN_1
#define I2C_SDA GPIO_PIN_2
static uint8_t g_ready = 0U;
static uint32_t g_cyclesPerUs = 0U;

/* 2026-11-16：DWT计数延时，不占用SysTick或蜂鸣器定时器。
 * 仅在主循环使用；若计数器停止，有限次数后将总线标为不可用。 */
static void i2c_delay(uint32_t delayUs)
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
static uint8_t i2c_raise(void)
{
    gpio_bit_set(GPIOB, I2C_SCL);
    for (uint32_t count = 0U; count < 200U; count++) {
        if (g_ready == 0U) {
            return 0U;
        }
        if (SET == gpio_input_bit_get(GPIOB, I2C_SCL)) {
            i2c_delay(5U);
            return g_ready;
        }
        i2c_delay(5U);
    }
    return 0U;
}

/* 2026-11-16：生成STOP；SCL被外部拉低时仍释放两根线。 */
static uint8_t i2c_stop(void)
{
    uint8_t status;
    gpio_bit_reset(GPIOB, I2C_SCL | I2C_SDA);
    i2c_delay(5U);
    status = i2c_raise();
    gpio_bit_set(GPIOB, I2C_SDA);
    i2c_delay(5U);
    return (uint8_t)((status != 0U) && (g_ready != 0U) &&
                    (SET == gpio_input_bit_get(GPIOB, I2C_SDA)));
}

/* 2026-11-16：发送8位并读取从机ACK，不假定屏幕一定在线。 */
static uint8_t i2c_byte(uint8_t value)
{
    uint8_t ack;
    for (uint32_t bit = 0U; bit < 8U; bit++) {
        gpio_bit_reset(GPIOB, I2C_SCL);
        gpio_bit_write(GPIOB, I2C_SDA, (value & 0x80U) ? SET : RESET);
        i2c_delay(5U);
        if (i2c_raise() == 0U) {
            return 0U;
        }
        gpio_bit_reset(GPIOB, I2C_SCL);
        value = (uint8_t)(value << 1U);
    }
    /* 第9个时钟由从机驱动SDA，主机必须释放SDA。 */
    gpio_bit_set(GPIOB, I2C_SDA);
    i2c_delay(5U);
    if (i2c_raise() == 0U) {
        return 0U;
    }
    ack = (uint8_t)(RESET == gpio_input_bit_get(GPIOB, I2C_SDA));
    gpio_bit_reset(GPIOB, I2C_SCL);
    i2c_delay(5U);
    return (uint8_t)((ack != 0U) && (g_ready != 0U));
}

/* 2026-11-16：初始化OLED独立总线，等待模块上电并尝试释放残留事务。 */
uint8_t i2c_init(void)
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
        i2c_delay(1000U);
    }
    for (uint32_t count = 0U; count < 9U; count++) {
        if (SET == gpio_input_bit_get(GPIOB, I2C_SDA)) {
            break;
        }
        gpio_bit_reset(GPIOB, I2C_SCL);
        i2c_delay(5U);
        if (i2c_raise() == 0U) {
            gpio_bit_set(GPIOB, I2C_SCL | I2C_SDA);
            return 0U;
        }
    }
    return i2c_stop();
}

/* 2026-11-16：OLED专用主机写事务，失败不自动重发半个命令序列。 */
uint8_t i2c_write(uint8_t address, const uint8_t *data, size_t length)
{
    uint8_t status = 1U;
    if ((g_ready == 0U) || (address > 0x7FU) ||
        ((data == NULL) && (length != 0U)) || (length > 129U)) {
        return 0U;
    }
    gpio_bit_set(GPIOB, I2C_SDA);
    if ((i2c_raise() == 0U) ||
        (RESET == gpio_input_bit_get(GPIOB, I2C_SDA))) {
        gpio_bit_set(GPIOB, I2C_SCL | I2C_SDA);
        return 0U;
    }
    /* SCL为高时拉低SDA生成START，再在低电平期间准备数据。 */
    gpio_bit_reset(GPIOB, I2C_SDA);
    i2c_delay(5U);
    gpio_bit_reset(GPIOB, I2C_SCL);
    status = i2c_byte((uint8_t)(address << 1U));
    for (size_t index = 0U; (index < length) && (status != 0U); index++) {
        status = i2c_byte(data[index]);
    }
    if (i2c_stop() == 0U) {
        status = 0U;
    }
    return status;
}
