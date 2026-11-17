#include "mid_measure.h"
#include "int_hc4066.h"
#include "int_hef4052.h"
#include "int_sgm58031.h"
#include "int_adc_i2c.h"
#include <stddef.h>

static uint8_t g_ready = 0U;
static uint8_t g_adcFault = 0U;

/* 2026-11-16：装配两颗开关驱动，保持ADC及其他器件初始化独立。 */
void mid_measure_init(void)
{
    hc4066_init();
    hef4052_init();
    g_adcFault = 0U;
    g_ready = 1U;
}

/* 2026-11-16：只返回记录值，不读取或改动硬件。 */
uint8_t mid_measure_fault(void)
{
    return g_adcFault;
}

/* 2026-11-16：先断后通并在任何错误路径撤销激励，避免半组结果外泄。 */
MeasureStatus mid_measure_read(MeasureRoute route, MeasureSample *sample)
{
    Hc4066Mode driveMode;
    Hef4052Channel inputChannel;
    SgmSample ain0;
    SgmSample ain1;
    SgmStatus adcStatus;
    MeasureStatus status = MEASURE_OK;
    MeasureSample result;

    hc4066_off();
    g_adcFault = 0U;
    if ((sample == NULL) || ((unsigned int)route > (unsigned int)MEASURE_WATER)) {
        return MEASURE_ARGUMENT;
    }
    if (g_ready == 0U) {
        return MEASURE_NOT_READY;
    }
    /* 激励组合与采样通道分别命名，防止把驱动侧和测量侧混为一谈。 */
    switch (route) {
        case MEASURE_REFERENCE:
            driveMode = HC4066_OFF;
            inputChannel = HEF4052_REFERENCE;
            break;
        case MEASURE_YELLOW_BLACK:
            driveMode = HC4066_YELLOW_BLACK;
            inputChannel = HEF4052_YELLOW_BLACK;
            break;
        case MEASURE_GREEN_YELLOW:
            driveMode = HC4066_YELLOW_GREEN;
            inputChannel = HEF4052_GREEN_YELLOW;
            break;
        case MEASURE_RED_GREEN:
            driveMode = HC4066_RED_GREEN;
            inputChannel = HEF4052_RED_GREEN;
            break;
        case MEASURE_WATER:
            /* 2026-11-16：供电与回流跨两个正常终端回路，测绿/黄是否耦合。 */
            driveMode = HC4066_RED_BLACK;
            inputChannel = HEF4052_GREEN_YELLOW;
            break;
        default:
            return MEASURE_ARGUMENT;
    }
    if (adc_i2c_wait(1U) == 0U) {
        return MEASURE_WAIT_ERROR;
    }
    if ((hef4052_select(inputChannel) == 0U) || (hc4066_set(driveMode) == 0U)) {
        hc4066_off();
        return MEASURE_SWITCH_ERROR;
    }
    if (adc_i2c_wait(10U) == 0U) {
        hc4066_off();
        return MEASURE_WAIT_ERROR;
    }
    adcStatus = sgm58031_read(0U, &ain0);
    if (adcStatus == SGM_OK) {
        adcStatus = sgm58031_read(1U, &ain1);
    }
    /* 两个通道采集结束立即撤销激励，不让串口输出时间延长通电。 */
    hc4066_off();
    g_adcFault = (uint8_t)adcStatus;
    if (adcStatus != SGM_OK) {
        status = MEASURE_ADC_ERROR;
    } else if ((ain0.saturated != 0U) || (ain1.saturated != 0U)) {
        status = MEASURE_SATURATED;
    } else {
        result.ain0Uv = ain0.microvolts;
        result.ain1Uv = ain1.microvolts;
        result.differenceUv = ain0.microvolts - ain1.microvolts;
        result.ain0Raw = ain0.raw;
        result.ain1Raw = ain1.raw;
        *sample = result;
    }
    return status;
}

/* 2026-11-17：固定激励时切换高阻ADC采样端，不重新开关4066。
 * 调用方负责最终关闭激励及确保整组结果不部分更新。 */
static MeasureStatus measure_locpair(Hef4052Channel channel, MeasureSample *sample, uint32_t *spreadUv)
{
    int32_t sumFirst = 0;
    int32_t sumSecond = 0;
    int32_t sumRawFirst = 0;
    int32_t sumRawSecond = 0;
    int32_t minimum = 0;
    int32_t maximum = 0;
    MeasureSample result;
    if (hef4052_select(channel) == 0U) {
        return MEASURE_SWITCH_ERROR;
    }
    if (adc_i2c_wait(10U) == 0U) {
        return MEASURE_WAIT_ERROR;
    }
    /* 2026-11-17：4对采样降低随机波动，不能消除静态偏移或提高固有精度。 */
    for (uint32_t count = 0U; count < 4U; count++) {
        SgmSample first;
        SgmSample second;
        SgmStatus adcStatus = sgm58031_read(0U, &first);
        int32_t difference;
        if (adcStatus == SGM_OK) {
            adcStatus = sgm58031_read(1U, &second);
        }
        g_adcFault = (uint8_t)adcStatus;
        if (adcStatus != SGM_OK) {
            return MEASURE_ADC_ERROR;
        }
        if ((first.saturated != 0U) || (second.saturated != 0U)) {
            return MEASURE_SATURATED;
        }
        difference = first.microvolts - second.microvolts;
        if ((count == 0U) || (difference < minimum)) {
            minimum = difference;
        }
        if ((count == 0U) || (difference > maximum)) {
            maximum = difference;
        }
        sumFirst += first.microvolts;
        sumSecond += second.microvolts;
        sumRawFirst += first.raw;
        sumRawSecond += second.raw;
    }
    result.ain0Uv = sumFirst / 4;
    result.ain1Uv = sumSecond / 4;
    result.differenceUv = result.ain0Uv - result.ain1Uv;
    result.ain0Raw = (int16_t)(sumRawFirst / 4);
    result.ain1Raw = (int16_t)(sumRawSecond / 4);
    *sample = result;
    *spreadUv = (uint32_t)(maximum - minimum);
    return MEASURE_OK;
}

/* 2026-11-17：一次固定激励的定位电压采集，先断后通，结束统一撤销激励。 */
MeasureStatus mid_measure_locate(LocateSample *sample)
{
    LocateSample result;
    MeasureStatus status;
    hc4066_off();
    g_adcFault = 0U;
    if (sample == NULL) {
        return MEASURE_ARGUMENT;
    }
    if (g_ready == 0U) {
        return MEASURE_NOT_READY;
    }
    if (adc_i2c_wait(1U) == 0U) {
        return MEASURE_WAIT_ERROR;
    }
    if ((hef4052_select(HEF4052_GREEN_YELLOW) == 0U) ||
        (hc4066_set(HC4066_RED_BLACK) == 0U)) {
        hc4066_off();
        return MEASURE_SWITCH_ERROR;
    }
    status = measure_locpair(HEF4052_GREEN_YELLOW, &result.greenYellow, &result.greenYellowSpreadUv);
    if (status == MEASURE_OK) {
        status = measure_locpair(HEF4052_REFERENCE, &result.reference, &result.referenceSpreadUv);
    }
    if (status == MEASURE_OK) {
        status = measure_locpair(HEF4052_RED_GREEN, &result.redGreen, &result.redGreenSpreadUv);
    }
    /* E接地不能软件禁止4052；仅切换高阻采样端且等待稳定，不假设无瞬态。 */
    hc4066_off();
    if (status == MEASURE_OK) {
        *sample = result;
    }
    return status;
}
