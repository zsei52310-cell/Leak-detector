#include "app_display.h"
#include "app_display_font.h"
#include "app_location.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 2026-11-17: Draw only within framebuffer bounds; used by all UI primitives. */
static void display_pixel(DisplayFrame *frame, int x, int y)
{
    if ((x >= 0) && (x < 128) && (y >= 0) && (y < 64)) {
        frame->pixels[(size_t)(y / 8) * 128U + (size_t)x] |= (uint8_t)(1U << (y % 8));
    }
}

/* 2026-11-17: Integer line primitive for the separator and compact status icons. */
static void display_line(DisplayFrame *frame, int x, int y, int endX, int endY)
{
    int dx = (endX > x) ? endX - x : x - endX;
    int dy = (endY > y) ? y - endY : endY - y;
    int stepX = (x < endX) ? 1 : -1;
    int stepY = (y < endY) ? 1 : -1;
    int error = dx + dy;
    for (unsigned int count = 0U; count < 128U; count++) {
        int twice;
        display_pixel(frame, x, y);
        if ((x == endX) && (y == endY)) {
            break;
        }
        twice = 2 * error;
        if (twice >= dy) { error += dy; x += stepX; }
        if (twice <= dx) { error += dx; y += stepY; }
    }
}

/* 2026-11-17: Internal UTF-8 strings only; glyph table is a small UI subset.
 * Unsupported/truncated text fails rather than silently drawing the wrong glyph. */
static uint8_t display_chinese(DisplayFrame *frame, int x, int y, const char *text, uint8_t size)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p != 0U) {
        uint32_t code;
        const DisplayGlyph *glyph = NULL;
        if (*p == ' ') { x += size / 2; p++; continue; }
        if (((p[0] & 0xF0U) != 0xE0U) || (p[1] == 0U) || (p[2] == 0U) ||
            ((p[1] & 0xC0U) != 0x80U) || ((p[2] & 0xC0U) != 0x80U)) {
            return 0U;
        }
        code = ((uint32_t)(p[0] & 15U) << 12) | ((uint32_t)(p[1] & 63U) << 6) | (p[2] & 63U);
        for (size_t i = 0U; i < sizeof(g_chinese) / sizeof(g_chinese[0]); i++) {
            if ((g_chinese[i].code == code) && (g_chinese[i].size == size)) {
                glyph = &g_chinese[i]; break;
            }
        }
        if ((glyph == NULL) || (x < 0) || (x + size > 128) || (y < 0) || (y + size > 64)) {
            return 0U;
        }
        for (int row = 0; row < size; row++) {
            for (int col = 0; col < size; col++) {
                if ((glyph->rows[row * 2 + col / 8] & (0x80U >> (col % 8))) != 0U) {
                    display_pixel(frame, x + col, y + row);
                }
            }
        }
        x += size; p += 3;
    }
    return 1U;
}

/* 2026-11-17: Large 2x digits use integer formatting, no floating point printf. */
static void display_number(DisplayFrame *frame, int x, int y, const char *text)
{
    static const char chars[] = "0123456789.-~";
    static const uint8_t digits[][5] = {
        {0x3E,0x51,0x49,0x45,0x3E}, {0,0x42,0x7F,0x40,0},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30}, {1,0x71,9,5,3},
        {0x36,0x49,0x49,0x49,0x36}, {6,0x49,0x49,0x29,0x1E},
        {0,0x60,0x60,0,0}, {8,8,8,8,8}, {8,4,8,0x10,8}
    };
    for (size_t n = 0U; (text[n] != '\0') && (n < 6U); n++) {
        const char *found = strchr(chars, text[n]);
        if (found == NULL) { return; }
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if ((digits[(size_t)(found - chars)][col] & (1U << row)) != 0U) {
                    display_pixel(frame, x + col * 2, y + row * 2);
                    display_pixel(frame, x + col * 2 + 1, y + row * 2);
                    display_pixel(frame, x + col * 2, y + row * 2 + 1);
                    display_pixel(frame, x + col * 2 + 1, y + row * 2 + 1);
                }
            }
        }
        x += (text[n] == '.') ? 6 : 12;
    }
}

/* 2026-11-17: Distinct check, droplet, broken cable, warning and checking icons. */
static void display_icon(DisplayFrame *frame, LeakState state)
{
    if (state == LEAK_NORMAL) {
        for (int y = -13; y <= 13; y++) {
            for (int x = -13; x <= 13; x++) {
                int radius = x*x + y*y;
                if ((radius >= 145) && (radius <= 174)) { display_pixel(frame, 21+x, 33+y); }
            }
        }
        display_line(frame, 13,33,19,39); display_line(frame, 19,39,29,27);
        display_line(frame, 13,34,19,40); display_line(frame, 19,40,29,28);
    } else if (state == LEAK_DETECTED) {
        display_line(frame,21,19,10,36); display_line(frame,21,19,32,36);
        display_line(frame,10,36,12,43); display_line(frame,32,36,30,43);
        display_line(frame,12,43,18,47); display_line(frame,30,43,24,47);
        display_line(frame,18,47,24,47); display_line(frame,21,31,21,39);
        display_pixel(frame,21,43); display_pixel(frame,22,43);
    } else if (state == LEAK_CABLE_BREAK) {
        display_line(frame,8,27,18,27); display_line(frame,18,27,18,32);
        display_line(frame,24,35,24,40); display_line(frame,24,40,34,40);
        display_line(frame,17,35,25,27); display_line(frame,17,40,25,32);
    } else if (state == LEAK_SENSOR_FAULT) {
        display_line(frame,21,20,7,45); display_line(frame,21,20,35,45);
        display_line(frame,7,45,35,45); display_line(frame,21,29,21,37);
        display_pixel(frame,21,41); display_pixel(frame,22,41);
    } else {
        display_line(frame,10,23,32,23); display_line(frame,10,45,32,45);
        display_line(frame,12,24,30,44); display_line(frame,30,24,12,44);
    }
}

/* 2026-11-17: Chinese dashboard; no old position displayed on normal/fault frames. */
uint8_t app_display_build(const DisplayInput *input, DisplayFrame *output)
{
    const char *stateText;
    const char *detail;
    const char *footer;
    uint8_t ok;
    if ((input == NULL) || (output == NULL) || (input->state < LEAK_CHECKING) ||
        (input->state > LEAK_SENSOR_FAULT)) { return 0U; }
    memset(output, 0, sizeof(*output));
    switch (input->state) {
        case LEAK_NORMAL: stateText="系统正常"; detail="线路正常"; footer="实时监测中"; break;
        case LEAK_DETECTED: stateText="漏水报警"; detail=NULL; footer="距起点 估算值"; break;
        case LEAK_CABLE_BREAK: stateText="线缆断开"; detail="检查接线"; footer="检查线缆"; break;
        case LEAK_SENSOR_FAULT: stateText="采样故障"; detail="检查采样"; footer="检查接线"; break;
        case LEAK_CHECKING: stateText="正在检测"; detail="等待确认"; footer="实时监测中"; break;
        default: return 0U;
    }
    ok = display_chinese(output,4,0,"漏水检测仪",12U);
    ok &= display_chinese(output,100,0,"监测",12U);
    display_line(output,4,15,123,15);
    display_icon(output,input->state);
    ok &= display_chinese(output,44,19,stateText,16U);
    if (input->state == LEAK_DETECTED) {
        if ((input->locationValid != 0U) && (input->distanceMm >= 0) &&
            (input->distanceMm <= LOCATION_LENGTH_MM)) {
            char number[10];
            int32_t cm = (input->distanceMm + 5) / 10;
            int length = snprintf(number,sizeof(number),"%s%ld.%02ld",
                                  (input->calibrated != 0U) ? "" : "~",
                                  (long)(cm / 100),(long)(cm % 100));
            if ((length <= 0) || ((size_t)length >= sizeof(number))) { return 0U; }
            display_number(output,44,37,number);
            ok &= display_chinese(output,104,38,"米",12U);
            if (input->calibrated != 0U) { footer="距起点"; }
        } else {
            display_number(output,59,37,"--");
            footer="定位中";
        }
    } else {
        ok &= display_chinese(output,48,38,detail,12U);
    }
    ok &= display_chinese(output,4,52,footer,12U);
    return ok;
}
