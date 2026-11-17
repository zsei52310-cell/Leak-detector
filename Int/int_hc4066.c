#include "int_hc4066.h"
#include "gd32f10x.h"

#define HC_GREEN GPIO_PIN_15
#define HC_YELLOW GPIO_PIN_3
#define HC_RED GPIO_PIN_4
#define HC_BLACK GPIO_PIN_5
static uint8_t g_ready = 0U;

/* 2026-11-16：先写低电平，再释放JTAG并切为输出，避免初始化误导通。 */
void hc4066_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_AF);
    gpio_bit_reset(GPIOA, HC_GREEN);
    gpio_bit_reset(GPIOB, HC_YELLOW | HC_RED | HC_BLACK);
    /* 只禁JTAG，绝不能选GPIO_SWJ_DISABLE_REMAP，否则会丢失SWD。 */
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, HC_GREEN);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, HC_YELLOW | HC_RED | HC_BLACK);
    g_ready = 1U;
    hc4066_off();
}

/* 2026-11-16：74HC4066使能高有效，拉低使所有激励与回流开关断开。 */
void hc4066_off(void)
{
    if (g_ready != 0U) {
        gpio_bit_reset(GPIOB, HC_YELLOW | HC_RED | HC_BLACK);
        gpio_bit_reset(GPIOA, HC_GREEN);
    }
}

/* 2026-11-16：只允许已定义测量组合，不暴露任意控制位掩码。 */
uint8_t hc4066_set(Hc4066Mode mode)
{
    hc4066_off();
    if (g_ready == 0U) {
        return 0U;
    }
    /* 先接参考电阻回流端，再接3.3V供电端。 */
    switch (mode) {
        case HC4066_OFF:
            return 1U;
        case HC4066_YELLOW_BLACK:
            gpio_bit_set(GPIOB, HC_BLACK);
            gpio_bit_set(GPIOB, HC_YELLOW);
            return 1U;
        case HC4066_RED_GREEN:
            gpio_bit_set(GPIOA, HC_GREEN);
            gpio_bit_set(GPIOB, HC_RED);
            return 1U;
        case HC4066_YELLOW_GREEN:
            gpio_bit_set(GPIOA, HC_GREEN);
            gpio_bit_set(GPIOB, HC_YELLOW);
            return 1U;
        case HC4066_RED_BLACK:
            /* 2026-11-16：浸水采样激励，先接黑回流再开红供电。 */
            gpio_bit_set(GPIOB, HC_BLACK);
            gpio_bit_set(GPIOB, HC_RED);
            return 1U;
        default:
            return 0U;
    }
}
