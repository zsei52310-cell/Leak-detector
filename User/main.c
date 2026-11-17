#include "gd32f10x.h"
#include "int_debug_uart.h"
#include "int_buzzer.h"
#include "int_oled.h"
#include "int_sgm58031.h"
#include "int_adc_i2c.h"
#include "mid_measure.h"
#include "int_hc4066.h"
#include "app_leak.h"
#include "app_location.h"
#include "app_display.h"
#include <stdio.h>
#include <string.h>

/* 2026-11-17: Only current leak episode may own a distance; main-loop only. */
static DisplayInput g_display = {LEAK_CHECKING, 0, 0U, 0U};

/* 2026-11-16：主循环测试日志，串口超时则停止测试，避免无声失败。 */
static void test_log(const char *text)
{
    if (debug_uart_write(text, (uint32_t)strlen(text)) == 0U) {
        hc4066_off();
        buzzer_stop();
        while (1) {
        }
    }
}

/* 2026-11-16：初始化失败后检查独立ADC总线，只扫描4个合法地址。 */
static void test_adc_bus(void)
{
    char line[96];
    int length;
    uint8_t busOk = adc_i2c_init();
    length = snprintf(line, sizeof(line), "ADC bus init=%u idle SCL=%u SDA=%u\r\n",
                      (unsigned int)busOk,
                      (gpio_input_bit_get(GPIOB, GPIO_PIN_8) == SET) ? 1U : 0U,
                      (gpio_input_bit_get(GPIOB, GPIO_PIN_9) == SET) ? 1U : 0U);
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        test_log(line);
    }
    if (busOk == 0U) {
        test_log("BUS NOT IDLE: check PB8/PB9, pullups, shorts, or timing\r\n");
        return;
    }
    for (uint8_t address = 0x48U; address <= 0x4BU; address++) {
        uint8_t ack = adc_i2c_probe(address);
        length = snprintf(line, sizeof(line), "ADC probe 0x%02X %s\r\n",
                          (unsigned int)address, ack ? "ACK" : "NO_ACK");
        if ((length > 0) && ((size_t)length < sizeof(line))) {
            test_log(line);
        }
        if (ack != 0U) {
            uint16_t id = 0U;
            uint8_t readOk = adc_i2c_read16(address, 0x05U, &id);
            length = snprintf(line, sizeof(line), "  ID read=%u value=0x%04X\r\n",
                              (unsigned int)readOk, (unsigned int)id);
            if ((length > 0) && ((size_t)length < sizeof(line))) {
                test_log(line);
            }
        }
    }
}

/* 2026-11-17: Render Chinese UI; unchanged frames do not spend I2C time.
 * A failed transfer never updates the cache, so the next call can retry. */
static uint8_t test_screen(void)
{
    static DisplayFrame frame;
    static DisplayInput previous;
    static uint8_t havePrevious = 0U;
    g_display.state = app_leak_state();
    if (g_display.state != LEAK_DETECTED) {
        g_display.locationValid = 0U;
        g_display.distanceMm = 0;
        g_display.calibrated = 0U;
    }
    if ((havePrevious != 0U) && (previous.state == g_display.state) &&
        (previous.distanceMm == g_display.distanceMm) &&
        (previous.locationValid == g_display.locationValid) &&
        (previous.calibrated == g_display.calibrated)) {
        return 1U;
    }
    if ((app_display_build(&g_display, &frame) == 0U) ||
        (oled_frame(frame.pixels, sizeof(frame.pixels)) == 0U)) {
        return 0U;
    }
    previous = g_display;
    havePrevious = 1U;
    return 1U;
}

/* 2026-11-17：本次漏水首次立即定位；之后间隔两轮，保留正常监测时间。
 * 每轮完整帧调用一次，非法/非浸水输入清除计数与旧距离；仅限主循环。 */
static uint8_t test_locdue(const LeakFrame *frame)
{
    static uint8_t g_waitCycles = 0U;
    if ((frame == NULL) || (frame->validMask != LEAK_VALID_ALL) ||
        (app_leak_state() != LEAK_DETECTED) ||
        (frame->wetGreenUv < LOCATION_GREEN_MIN_UV) ||
        (frame->wetYellowUv < LOCATION_YELLOW_MIN_UV)) {
        g_waitCycles = 0U;
        g_display.locationValid = 0U;
        g_display.distanceMm = 0;
        g_display.calibrated = 0U;
        return 0U;
    }
    if (g_waitCycles == 0U) {
        g_waitCycles = 2U;
        return 1U;
    }
    g_waitCycles--;
    return 0U;
}

/* 2026-11-17：整组成功后再打印，编号用于对应同一固定激励下的数据。 */
static void test_locate(void)
{
    static uint32_t g_captureId = 0U;
    LocateSample capture;
    MeasureSample verify = {0};
    MeasureStatus status = mid_measure_locate(&capture);
    MeasureStatus checked;
    int64_t greenChange;
    int64_t yellowChange;
    uint8_t fresh;
    LocationInput input;
    LocationResult position;
    LocationStatus located;
    char line[112];
    int length;
    g_captureId++;
    /* Failed/unstable captures invalidate old distance immediately. */
    g_display.locationValid = 0U;
    g_display.distanceMm = 0;
    g_display.calibrated = 0U;
    if (status != MEASURE_OK) {
        length = snprintf(line, sizeof(line), "LOC id=%lu FAIL measure=%u adc=%u\r\n",
                          (unsigned long)g_captureId, (unsigned int)status,
                          (unsigned int)mid_measure_fault());
        if ((length > 0) && ((size_t)length < sizeof(line))) {
            test_log(line);
        }
        return;
    }
    /* 2026-11-17：记录任何日志前复测，避免采集过程中擦干后发布旧距离。
     * 不以这一次复测推进报警状态机，报警仍由完整新帧和原消警条件控制。 */
    checked = mid_measure_read(MEASURE_WATER, &verify);
    greenChange = (int64_t)verify.ain0Uv - capture.greenYellow.ain0Uv;
    yellowChange = (int64_t)verify.ain1Uv - capture.greenYellow.ain1Uv;
    fresh = (uint8_t)((checked == MEASURE_OK) &&
                      (verify.ain0Uv >= LOCATION_GREEN_MIN_UV) && (verify.ain0Uv <= 3600000) &&
                      (verify.ain1Uv >= LOCATION_YELLOW_MIN_UV) && (verify.ain1Uv <= 3600000) &&
                      (greenChange >= -LOCATION_RECHECK_MAX_UV) &&
                      (greenChange <= LOCATION_RECHECK_MAX_UV) &&
                      (yellowChange >= -LOCATION_RECHECK_MAX_UV) &&
                      (yellowChange <= LOCATION_RECHECK_MAX_UV));
    length = snprintf(line, sizeof(line), "LOC_CHECK id=%lu status=%u GREEN_uV=%ld YELLOW_uV=%ld fresh=%u\r\n",
                      (unsigned long)g_captureId, (unsigned int)checked,
                      (long)verify.ain0Uv, (long)verify.ain1Uv, (unsigned int)fresh);
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        test_log(line);
    }
    /* 差值均为前者减后者，单位uV；REF是有激励的电阻压降，不是旧REF自检。 */
    length = snprintf(line, sizeof(line), "LOC_GY id=%lu GREEN_uV=%ld YELLOW_uV=%ld diff_uV=%ld\r\n",
                      (unsigned long)g_captureId, (long)capture.greenYellow.ain0Uv,
                      (long)capture.greenYellow.ain1Uv, (long)capture.greenYellow.differenceUv);
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        test_log(line);
    }
    length = snprintf(line, sizeof(line), "LOC_REF id=%lu UPPER_uV=%ld LOWER_uV=%ld drop_uV=%ld\r\n",
                      (unsigned long)g_captureId, (long)capture.reference.ain0Uv,
                      (long)capture.reference.ain1Uv, (long)capture.reference.differenceUv);
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        test_log(line);
    }
    length = snprintf(line, sizeof(line), "LOC_RG id=%lu RED_uV=%ld GREEN_uV=%ld diff_uV=%ld\r\n",
                      (unsigned long)g_captureId, (long)capture.redGreen.ain0Uv,
                      (long)capture.redGreen.ain1Uv, (long)capture.redGreen.differenceUv);
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        test_log(line);
    }
    input.greenUv = capture.greenYellow.ain0Uv;
    input.yellowUv = capture.greenYellow.ain1Uv;
    input.referenceDropUv = capture.reference.differenceUv;
    input.redGreenDropUv = capture.redGreen.differenceUv;
    input.referenceSpreadUv = capture.referenceSpreadUv;
    input.redGreenSpreadUv = capture.redGreenSpreadUv;
    input.leakConfirmed = (uint8_t)(app_leak_state() == LEAK_DETECTED);
    located = (fresh != 0U) ? app_location_calc(&input, &position) : LOCATION_UNSTABLE;
    if ((located == LOCATION_ESTIMATE) || (located == LOCATION_VALID)) {
        g_display.distanceMm = position.distanceMm;
        g_display.locationValid = 1U;
        g_display.calibrated = position.validated;
        length = snprintf(line, sizeof(line), "LOC_EST id=%lu mm=%ld remain_mOhm=%ld I_uA=%ld validated=%u\r\n",
                          (unsigned long)g_captureId, (long)position.distanceMm,
                          (long)position.remainingMilliOhm, (long)position.currentUa,
                          (unsigned int)position.validated);
    } else {
        length = snprintf(line, sizeof(line), "LOC_NONE id=%lu reason=%u ref_span_uV=%lu rg_span_uV=%lu\r\n",
                          (unsigned long)g_captureId, (unsigned int)located,
                          (unsigned long)capture.referenceSpreadUv,
                          (unsigned long)capture.redGreenSpreadUv);
    }
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        test_log(line);
    }

}

/* 2026-11-16：完整采样轮次驱动App状态机；此版按实测浸水日志验证报警流程，尚未标定不同水质。 */
int main(void)
{
    const MeasureRoute routes[] = {MEASURE_YELLOW_BLACK, MEASURE_RED_GREEN, MEASURE_WATER};
    const char *const routeNames[] = {"YB_LOOP", "RG_LOOP", "WATER"};
    char line[112];
    uint16_t chipId = 0U;
    SgmStatus status;
    int length;
    uint8_t screenOk;
    LeakState previousState = LEAK_CHECKING;
    uint8_t locateDue;

    mid_measure_init();
    buzzer_init();
    debug_uart_init();
    app_leak_init();
    test_log("Leak alarm demo: water-trial thresholds, calibration pending\r\n");
    screenOk = oled_init();
    if (screenOk != 0U) {
        screenOk = test_screen();
    }
    if (screenOk == 0U) {
        test_log("OLED unavailable; UART and alarm continue\r\n");
    }
    status = sgm58031_init();
    if (status == SGM_OK) {
        status = sgm58031_id(&chipId);
    }
    if (status != SGM_OK) {
        app_leak_update(NULL);
        if ((screenOk != 0U) && (test_screen() == 0U)) {
            test_log("OLED refresh FAIL\r\n");
        }
        length = snprintf(line, sizeof(line), "STATE=FAULT ADC init=%u step=%u; reset after repair\r\n",
                          (unsigned int)status, (unsigned int)sgm58031_step());
        if ((length > 0) && ((size_t)length < sizeof(line))) {
            test_log(line);
        }
        test_adc_bus();
        if (buzzer_beep(500U) == 0U) {
            test_log("Buzzer start FAIL\r\n");
        }
        while (1) {
            __WFI();
        }
    }
    length = snprintf(line, sizeof(line), "SGM58031 ready, chip_id=0x%04X; STATE=CHECK\r\n",
                      (unsigned int)chipId);
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        test_log(line);
    }
    test_log("WATER AIN0=GREEN AIN1=YELLOW; each cycle uses 3 fresh measurements\r\n");
    test_log("Confirm: wet=3 cycles, dry=2 cycles, loop fault=3 cycles; dry YELLOW<=0.50V; unit=uV\r\n");
    test_log("Water trial v2: GREEN>=2.00V YELLOW>=0.80V; Locate v3: immediate + REF<=3%/50mV + final wet recheck\r\n");
    test_log("LOC_GY/REF/RG: fixed RED power + BLACK return, 4-pair averages; distance is an UNVALIDATED estimate\r\n");

    while (1) {
        LeakFrame frame = {0};
        /* 每轮清空有效位；任何采样失败都不会沿用上一轮读数。 */
        for (size_t route = 0U; route < sizeof(routes) / sizeof(routes[0]); route++) {
            MeasureSample sample;
            MeasureStatus measured = mid_measure_read(routes[route], &sample);
            if (measured == MEASURE_OK) {
                frame.validMask |= (uint8_t)(1U << route);
                if (route == 0U) {
                    frame.yellowSourceUv = sample.ain0Uv;
                    frame.blackReturnUv = sample.ain1Uv;
                } else if (route == 1U) {
                    frame.redSourceUv = sample.ain0Uv;
                    frame.greenReturnUv = sample.ain1Uv;
                } else {
                    frame.wetGreenUv = sample.ain0Uv;
                    frame.wetYellowUv = sample.ain1Uv;
                }
                length = snprintf(line, sizeof(line), "%s AIN0_uV=%ld AIN1_uV=%ld diff_uV=%ld\r\n",
                                  routeNames[route], (long)sample.ain0Uv,
                                  (long)sample.ain1Uv, (long)sample.differenceUv);
            } else {
                length = snprintf(line, sizeof(line), "%s FAIL measure=%u adc=%u\r\n",
                                  routeNames[route], (unsigned int)measured,
                                  (unsigned int)mid_measure_fault());
            }
            if ((length > 0) && ((size_t)length < sizeof(line))) {
                test_log(line);
            }
            if (adc_i2c_wait(10U) == 0U) {
                hc4066_off();
                app_leak_update(NULL);
                if (screenOk != 0U) {
                    screenOk = test_screen();
                }
                test_log("STATE=FAULT timing stopped; reset required\r\n");
                if (buzzer_beep(500U) == 0U) {
                    test_log("Buzzer start FAIL\r\n");
                }
                while (1) {
                    __WFI();
                }
            }
        }
        app_leak_update(&frame);
        if (app_leak_state() != previousState) {
            previousState = app_leak_state();
            length = snprintf(line, sizeof(line), "STATE=%s alarm=%u\r\n",
                              app_leak_name(), (unsigned int)app_leak_alarm());
            if ((length > 0) && ((size_t)length < sizeof(line))) {
                test_log(line);
            }
            if (screenOk != 0U) {
                screenOk = test_screen();
                if (screenOk == 0U) {
                    test_log("OLED refresh FAIL; UART and alarm continue\r\n");
                }
            }
        }
        locateDue = test_locdue(&frame);
        /* 先处理声音；定位轮保持安静，避免蜂鸣器开关干扰微伏级采样。
         * 正常状态立即停止；断线/采样故障仍使用独立短鸣提示。 */
        if ((app_leak_alarm() != 0U) && (locateDue == 0U)) {
            uint32_t beepMs = (app_leak_state() == LEAK_DETECTED) ? 300U : 150U;
            if (buzzer_beep(beepMs) == 0U) {
                test_log("Buzzer start FAIL\r\n");
            }
        } else {
            buzzer_stop();
        }
        if (locateDue != 0U) {
            test_locate();
        }
        if (screenOk != 0U) {
            screenOk = test_screen();
            if (screenOk == 0U) {
                test_log("OLED refresh FAIL; UART and alarm continue\r\n");
            }
        }
        if (adc_i2c_wait(100U) == 0U) {
            hc4066_off();
            app_leak_update(NULL);
            if (screenOk != 0U) {
                screenOk = test_screen();
            }
            test_log("STATE=FAULT timing stopped; reset required\r\n");
            if (buzzer_beep(500U) == 0U) {
                test_log("Buzzer start FAIL\r\n");
            }
            while (1) {
                __WFI();
            }
        }
    }
}
