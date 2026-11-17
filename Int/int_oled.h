#ifndef INT_OLED_H
#define INT_OLED_H
#include <stddef.h>
#include <stdint.h>

/* SSD1315 128x64，探测0x3C/0x3D；主循环调用，返回1成功、0失败。 */
uint8_t oled_init(void);
/* 仅清RAM画面；调用oled_refresh后才更新实屏。 */
void oled_clear(void);
/* 5x7字模，x为像素列，page为8像素高的行(0~7)。
 * text不可为空，调用期间有效；length不包含结束符。
 * 字库覆盖Leak Detector、OLED OK和CHECK/NORMAL/LEAK/BREAK/FAULT；其他字符返回0。
 * 不换行、不越界、不修改调用者数据，失败时不更新RAM画面。 */
uint8_t oled_text(uint8_t x, uint8_t page, const char *text, size_t length);
/* 刷新1024字节RAM画面。失败可能只刷新了部分屏幕。 */
uint8_t oled_refresh(void);
/* 2026-11-17: Copy exactly 1024 page-format bytes into RAM then refresh.
 * Non-null pixels valid during call only; no pointer retained. Invalid arguments
 * leave RAM unchanged; I2C failure may have partially updated the physical panel. */
uint8_t oled_frame(const uint8_t *pixels, size_t length);
#endif
