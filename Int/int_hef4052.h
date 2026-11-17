#ifndef INT_HEF4052_H
#define INT_HEF4052_H
#include <stdint.h>

typedef enum {
    HEF4052_YELLOW_BLACK = 0, /* AIN0=黄，AIN1=黑 */
    HEF4052_GREEN_YELLOW = 1, /* AIN0=绿，AIN1=黄 */
    HEF4052_RED_GREEN = 2,    /* AIN0=红，AIN1=绿 */
    HEF4052_REFERENCE = 3     /* AIN0=参考上端1Y3，AIN1=下端2Y3 */
} Hef4052Channel;

/* 主循环初始化：PB6=S1(低位)，PB7=S2(高位)，初始选择参考通道。
 * 板上E直接接地，软件不能将4052全部关闭；改变激励前须关闭4066。 */
void hef4052_init(void);
/* 返回1成功、0表示未初始化或参数错误；无效参数不改选择脚。
 * 固定激励下允许切换高阻ADC采样端，必须等待稳定后再采样。
 * 不负责等待模拟电压稳定，不可在ISR或并发调用。 */
uint8_t hef4052_select(Hef4052Channel channel);
#endif
