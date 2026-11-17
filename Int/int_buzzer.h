#ifndef INT_BUZZER_H
#define INT_BUZZER_H

#include <stdint.h>

/* PA4驱动无源蜂鸣器，占用TIMER1及其更新中断；主循环先调用一次。 */
void buzzer_init(void);
/* 主循环调用：约2.7kHz，自动停止；durationMs为1~5000ms。
 * 返回1表示启动成功，返回0表示未初始化或参数无效。不阻塞等待。 */
uint8_t buzzer_beep(uint32_t durationMs);
/* 主循环或本模块ISR调用，立即停止并将PA4拉低。 */
void buzzer_stop(void);

#endif
