#include "int_oled.h"
#include "int_i2c.h"
#include <string.h>

static uint8_t g_frame[1024];
static uint8_t g_address = 0U;
static uint8_t g_oledReady = 0U;
/* 自绘5x7列字模，最低位对应顶部像素，仅覆盖本次测试用字。 */
static const char g_letters[] = " LeakDtc orOEKCHNRMABFUT-";
static const uint8_t g_glyphs[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x38,0x44,0x44,0x44,0x28}, /* c */
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    /* 2026-11-16：报警状态CHECK/NORMAL/LEAK/BREAK/FAULT需要的字符。 */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x7F,0x02,0x04,0x08,0x7F}, /* N */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7E,0x09,0x09,0x09,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x08,0x08,0x08,0x08,0x08}  /* - */
};

/* 2026-11-16：查找测试字模；不支持的字符不静默替换。 */
static const uint8_t *oled_glyph(char value)
{
    for (size_t index = 0U; index < sizeof(g_letters) - 1U; index++) {
        if (g_letters[index] == value) {
            return g_glyphs[index];
        }
    }
    return NULL;
}

/* 2026-11-16：清除软件显存，不操作I2C。 */
void oled_clear(void)
{
    memset(g_frame, 0, sizeof(g_frame));
}

/* 2026-11-16：SSD1315初始化；命令依据SSD1315 Rev1.1，7.5V电荷泵模式。 */
uint8_t oled_init(void)
{
    const uint8_t commands[] = {
        0x00, /* I2C control: following bytes are commands */
        0xAE, 0xD5,0x80, 0xA8,0x3F, 0xD3,0x00, 0x40,
        0x8D,0x14, 0x20,0x02, /* charge pump on, page addressing */
        0xA1, 0xC8, 0xDA,0x12, 0x81,0x7F,
        0xD9,0xF1, 0xDB,0x20, 0xA4, 0xA6, 0x2E
    };
    const uint8_t displayOn[] = {0x00, 0xAF};
    g_oledReady = 0U;
    if (i2c_init() == 0U) {
        return 0U;
    }
    if (i2c_write(0x3CU, NULL, 0U) != 0U) {
        g_address = 0x3CU;
    } else if (i2c_write(0x3DU, NULL, 0U) != 0U) {
        g_address = 0x3DU;
    } else {
        return 0U;
    }
    if (i2c_write(g_address, commands, sizeof(commands)) == 0U) {
        return 0U;
    }
    /* 清除上电随机显存后再亮屏，减少闪屏。 */
    g_oledReady = 1U;
    oled_clear();
    if ((oled_refresh() == 0U) ||
        (i2c_write(g_address, displayOn, sizeof(displayOn)) == 0U)) {
        g_oledReady = 0U;
        return 0U;
    }
    return 1U;
}

/* 2026-11-16：先检查整段文本，再写显存，防止越界或半段更新。 */
uint8_t oled_text(uint8_t x, uint8_t page, const char *text, size_t length)
{
    if ((text == NULL) || (page >= 8U) || (x >= 128U) ||
        (length > (128U - x) / 6U)) {
        return 0U;
    }
    for (size_t index = 0U; index < length; index++) {
        if (oled_glyph(text[index]) == NULL) {
            return 0U;
        }
    }
    for (size_t index = 0U; index < length; index++) {
        size_t offset = (size_t)page * 128U + x + index * 6U;
        memcpy(&g_frame[offset], oled_glyph(text[index]), 5U);
        g_frame[offset + 5U] = 0U;
    }
    return 1U;
}

/* 2026-11-16：逐页定位并发送像素；任一事务失败就交给调用者报告。 */
uint8_t oled_refresh(void)
{
    uint8_t packet[129];
    if (g_oledReady == 0U) {
        return 0U;
    }
    for (uint8_t page = 0U; page < 8U; page++) {
        uint8_t position[] = {0x00, (uint8_t)(0xB0U + page), 0x00, 0x10};
        if (i2c_write(g_address, position, sizeof(position)) == 0U) {
            return 0U;
        }
        packet[0] = 0x40U; /* data stream */
        memcpy(&packet[1], &g_frame[(size_t)page * 128U], 128U);
        if (i2c_write(g_address, packet, sizeof(packet)) == 0U) {
            return 0U;
        }
    }
    return 1U;
}

/* 2026-11-17: UI rendering stays outside the SSD1315 transport driver. */
uint8_t oled_frame(const uint8_t *pixels, size_t length)
{
    if ((pixels == NULL) || (length != sizeof(g_frame)) || (g_oledReady == 0U)) {
        return 0U;
    }
    memcpy(g_frame, pixels, sizeof(g_frame));
    return oled_refresh();
}
