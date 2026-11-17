#include "int_buzzer.h"
#include "gd32f10x.h"

#define BUZZER_TICK_HZ       1000000U
#define BUZZER_EDGE_HZ       5400U
#define BUZZER_MAX_MS        5000U

/* 主循环在TIMER1 IRQ关闭时更新，ISR负责递减；不与其他模块共享。 */
static volatile uint32_t g_edgesLeft = 0U;
static volatile uint8_t g_pinHigh = 0U;
static uint8_t g_ready = 0U;

/* 2026-11-16：初始化PA4和TIMER1，避免初始化阶段误响。 */
void buzzer_init(void)
{
    timer_parameter_struct timerConfig;
    uint32_t timerClockHz;
    uint32_t divider;

    NVIC_DisableIRQ(TIMER1_IRQn);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_TIMER1);

    /* 先设置输出锁存值为低，再切为推挽输出，MOS管默认关闭。 */
    gpio_bit_reset(GPIOA, GPIO_PIN_4);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_4);
    timer_deinit(TIMER1);
    g_ready = 0U;
    g_edgesLeft = 0U;
    g_pinHigh = 0U;

    /* APB1不分频时定时器时钟等于PCLK1，否则为PCLK1的两倍。 */
    timerClockHz = rcu_clock_freq_get(CK_APB1);
    if (timerClockHz != rcu_clock_freq_get(CK_AHB)) {
        timerClockHz *= 2U;
    }
    divider = timerClockHz / BUZZER_TICK_HZ;
    if ((divider == 0U) || (divider > 65536U) ||
        ((timerClockHz % BUZZER_TICK_HZ) != 0U)) {
        return;
    }

    timer_struct_para_init(&timerConfig);
    timerConfig.prescaler = (uint16_t)(divider - 1U);
    timerConfig.alignedmode = TIMER_COUNTER_EDGE;
    timerConfig.counterdirection = TIMER_COUNTER_UP;
    /* 每约185us翻转一次，两个翻转组成一个周期，频率约2703Hz。 */
    timerConfig.period = ((BUZZER_TICK_HZ + BUZZER_EDGE_HZ / 2U) /
                           BUZZER_EDGE_HZ) - 1U;
    timerConfig.clockdivision = TIMER_CKDIV_DIV1;
    timer_init(TIMER1, &timerConfig);
    timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_SetPriority(TIMER1_IRQn, 2U);
    g_ready = 1U;
}

/* 2026-11-16：停止波形并关闭MOS管；不等待、不输出串口日志。 */
void buzzer_stop(void)
{
    if (g_ready == 0U) {
        return;
    }

    /* 禁止本定时器中断后再改共享状态，防止主循环与ISR交错更新。 */
    NVIC_DisableIRQ(TIMER1_IRQn);
    timer_interrupt_disable(TIMER1, TIMER_INT_UP);
    timer_disable(TIMER1);
    timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    gpio_bit_reset(GPIOA, GPIO_PIN_4);
    g_pinHigh = 0U;
    g_edgesLeft = 0U;
}

/* 2026-11-16：启动有时限的蜂鸣；重复调用时重新计时。 */
uint8_t buzzer_beep(uint32_t durationMs)
{
    if ((g_ready == 0U) || (durationMs == 0U) ||
        (durationMs > BUZZER_MAX_MS)) {
        return 0U;
    }

    buzzer_stop();
    /* 限制时长后，乘法在uint32_t范围内；允许毫秒级近似误差。 */
    g_edgesLeft = (durationMs * BUZZER_EDGE_HZ + 999U) / 1000U;
    timer_counter_value_config(TIMER1, 0U);
    gpio_bit_set(GPIOA, GPIO_PIN_4);
    g_pinHigh = 1U;
    timer_interrupt_enable(TIMER1, TIMER_INT_UP);
    NVIC_EnableIRQ(TIMER1_IRQn);
    timer_enable(TIMER1);
    return 1U;
}

/* 2026-11-16：由启动向量调用；每次更新仅翻转GPIO或结束发声。 */
void TIMER1_IRQHandler(void)
{
    if (RESET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP)) {
        return;
    }
    timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);

    /* 正常结束或意外无剩余计数时，都确保输出回到低电平。 */
    if (g_edgesLeft > 0U) {
        g_edgesLeft--;
    }
    if (g_edgesLeft == 0U) {
        buzzer_stop();
        return;
    }

    if (g_pinHigh != 0U) {
        gpio_bit_reset(GPIOA, GPIO_PIN_4);
        g_pinHigh = 0U;
    } else {
        gpio_bit_set(GPIOA, GPIO_PIN_4);
        g_pinHigh = 1U;
    }
}
