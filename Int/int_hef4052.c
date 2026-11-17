#include "int_hef4052.h"
#include "gd32f10x.h"

#define MUX_S1 GPIO_PIN_6
#define MUX_S2 GPIO_PIN_7
static uint8_t g_ready = 0U;

/* 2026-11-16：初始化前由测量层先关闭4066，初始选择参考电阻。 */
void hef4052_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_bit_set(GPIOB, MUX_S1 | MUX_S2);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, MUX_S1 | MUX_S2);
    g_ready = 1U;
}

/* 2026-11-16：按数据手册S2/S1二进制选择Yn，GPIO位一次更新。
 * 改变激励前由调用者关闭4066；固定激励下切换高阻采样端须等待稳定。 */
uint8_t hef4052_select(Hef4052Channel channel)
{
    uint32_t setPins = 0U;
    uint32_t resetPins;
    if ((g_ready == 0U) || ((unsigned int)channel > 3U)) {
        return 0U;
    }
    if (((unsigned int)channel & 1U) != 0U) {
        setPins |= MUX_S1;
    }
    if (((unsigned int)channel & 2U) != 0U) {
        setPins |= MUX_S2;
    }
    resetPins = (MUX_S1 | MUX_S2) & ~setPins;
    /* BOP低16位置位、高16位复位，同一次寄存器写完成两脚切换。 */
    GPIO_BOP(GPIOB) = setPins | (resetPins << 16U);
    return 1U;
}
