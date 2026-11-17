#ifndef INT_HC4066_H
#define INT_HC4066_H
#include <stdint.h>

typedef enum {
    HC4066_OFF = 0,
    HC4066_YELLOW_BLACK,
    HC4066_RED_GREEN,
    HC4066_YELLOW_GREEN,
    HC4066_RED_BLACK /* 红线供电，黑线接参考电阻，绿/黄开关关闭 */
} Hc4066Mode;

/* 主循环初始化：PA15=绿回路，PB3=黄供电，PB4=红供电，PB5=黑回路。
 * 关闭JTAG释放前三个引脚，保留PA13/PA14的SWD下载与调试。
 * 禁止ISR/并发调用，以上引脚由本模块独占。 */
void hc4066_init(void);
/* 主循环立即关闭四路开关；未初始化时无操作。 */
void hc4066_off(void);
/* 返回1成功、0表示未初始化或模式错误；错误时保持全关。
 * 每次先全关再设置，调用者应先off并安排死区与模拟稳定时间。 */
uint8_t hc4066_set(Hc4066Mode mode);
#endif
