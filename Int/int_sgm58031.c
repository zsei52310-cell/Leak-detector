#include "int_sgm58031.h"
#include "int_adc_i2c.h"
#include <stddef.h>

#define SGM_ADDRESS      0x48U
#define SGM_CONVERSION   0x00U
#define SGM_CONFIG       0x01U
#define SGM_CONFIG1      0x04U
#define SGM_CHIP_ID      0x05U
#define SGM_OS           0x8000U
/* PGA=001(±4.096V), MODE=1, DR=100(100SPS), COMP_QUE=11。 */
#define SGM_BASE         0x0383U
static uint8_t g_initialized = 0U;
static uint8_t g_initStep = 0U;

/* 2026-11-16：先终止残留转换，再明确配置内部基准并回读校验。
 * 依据SGMICRO SGM58031 Rev.A.3，而非ADS1115兼容配置猜测。 */
SgmStatus sgm58031_init(void)
{
    uint16_t readback;
    g_initialized = 0U;
    g_initStep = 1U;
    if (adc_i2c_init() == 0U) {
        return SGM_BUS_ERROR;
    }
    /* 分开记录每一步，避免总线错误仅剩一个笼统的返回值。 */
    g_initStep = 2U;
    if (adc_i2c_write16(SGM_ADDRESS, SGM_CONFIG1, 0x0100U) == 0U) {
        return SGM_BUS_ERROR;
    }
    g_initStep = 3U;
    if (adc_i2c_wait(1U) == 0U) {
        return SGM_BUS_ERROR;
    }
    g_initStep = 4U;
    if (adc_i2c_write16(SGM_ADDRESS, SGM_CONFIG1, 0x0000U) == 0U) {
        return SGM_BUS_ERROR;
    }
    g_initStep = 5U;
    if (adc_i2c_read16(SGM_ADDRESS, SGM_CONFIG1, &readback) == 0U) {
        return SGM_BUS_ERROR;
    }
    g_initStep = 6U;
    if ((readback & 0x01D8U) != 0U) {
        return SGM_CONFIG_ERROR;
    }
    g_initStep = 7U;
    if (adc_i2c_write16(SGM_ADDRESS, SGM_CONFIG, SGM_BASE) == 0U) {
        return SGM_BUS_ERROR;
    }
    g_initStep = 8U;
    if (adc_i2c_read16(SGM_ADDRESS, SGM_CONFIG, &readback) == 0U) {
        return SGM_BUS_ERROR;
    }
    g_initStep = 9U;
    if ((readback & 0x7FFFU) != SGM_BASE) {
        return SGM_CONFIG_ERROR;
    }
    g_initialized = 1U;
    g_initStep = 0U;
    return SGM_OK;
}

/* 2026-11-16：读取ID和子版本原值，硬件测试阶段通过串口报告。 */
SgmStatus sgm58031_id(uint16_t *id)
{
    if (id == NULL) {
        return SGM_ARGUMENT;
    }
    if (g_initialized == 0U) {
        return SGM_NOT_READY;
    }
    if (adc_i2c_read16(SGM_ADDRESS, SGM_CHIP_ID, id) == 0U) {
        return SGM_BUS_ERROR;
    }
    return SGM_OK;
}

/* 2026-11-16：启动选定单端通道，等待OS就绪，再读有符号16位结果。 */
SgmStatus sgm58031_read(uint8_t channel, SgmSample *sample)
{
    uint16_t config;
    uint16_t readback;
    uint16_t rawWord;
    int32_t signedRaw;
    SgmSample result;
    if ((channel > 3U) || (sample == NULL)) {
        return SGM_ARGUMENT;
    }
    if (g_initialized == 0U) {
        return SGM_NOT_READY;
    }
    if (adc_i2c_read16(SGM_ADDRESS, SGM_CONFIG, &readback) == 0U) {
        return SGM_BUS_ERROR;
    }
    if ((readback & SGM_OS) == 0U) {
        return SGM_BUSY;
    }
    config = (uint16_t)(SGM_OS | SGM_BASE | ((uint32_t)(channel + 4U) << 12U));
    if (adc_i2c_write16(SGM_ADDRESS, SGM_CONFIG, config) == 0U) {
        return SGM_BUS_ERROR;
    }
    /* 100SPS的Sinc3滤波需多个周期；以OS为准，不只固定等10ms。 */
    for (uint32_t count = 0U; count < 100U; count++) {
        if ((adc_i2c_wait(1U) == 0U) ||
            (adc_i2c_read16(SGM_ADDRESS, SGM_CONFIG, &readback) == 0U)) {
            return SGM_BUS_ERROR;
        }
        if ((readback & 0x7FFFU) != (config & 0x7FFFU)) {
            return SGM_CONFIG_ERROR;
        }
        if ((readback & SGM_OS) != 0U) {
            if (adc_i2c_read16(SGM_ADDRESS, SGM_CONVERSION, &rawWord) == 0U) {
                return SGM_BUS_ERROR;
            }
            /* 显式处理二补码，避免超范围unsigned到int16转换的实现差异。 */
            signedRaw = (int32_t)rawWord;
            if ((rawWord & 0x8000U) != 0U) {
                signedRaw -= 65536;
            }
            result.raw = (int16_t)signedRaw;
            /* ±4.096V / 32768 = 125uV/LSB，避免浮点及基准电压混用。 */
            result.microvolts = signedRaw * 125;
            result.saturated = (uint8_t)((rawWord == 0x7FFFU) || (rawWord == 0x8000U));
            *sample = result;
            return SGM_OK;
        }
    }
    return SGM_TIMEOUT;
}

/* 2026-11-16：向测试入口提供初始化失败位置，不产生硬件副作用。 */
uint8_t sgm58031_step(void)
{
    return g_initStep;
}
