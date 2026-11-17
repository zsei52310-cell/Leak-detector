#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H
#include <stdint.h>
#include "app_leak.h"

typedef struct {
    LeakState state;
    int32_t distanceMm;
    uint8_t locationValid;
    uint8_t calibrated;
} DisplayInput;

typedef struct {
    uint8_t pixels[1024];
} DisplayFrame;

/* 2026-11-17: Build a complete Chinese 128x64 monochrome UI, no hardware access.
 * Main-loop only; non-null input/output, no retained pointers. Returns 1 on success.
 * Distance is ignored outside LEAK or when invalid/out of range; failed arguments
 * leave output unchanged. Pixel storage: 8 pages, LSB is the upper pixel. */
uint8_t app_display_build(const DisplayInput *input, DisplayFrame *output);
#endif
