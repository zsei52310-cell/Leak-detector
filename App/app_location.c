#include "app_location.h"
#include <stddef.h>

/* 2026-11-17：限定输入范围后再相减及使用64位中间量，避免异常数据溢出。 */
LocationStatus app_location_calc(const LocationInput *input, LocationResult *result)
{
    int64_t referenceUv;
    int64_t wireUv;
    int64_t remaining;
    int64_t distance;
    LocationResult output;
    if ((input == NULL) || (result == NULL)) {
        return LOCATION_ARGUMENT;
    }
    if (input->leakConfirmed == 0U) {
        return LOCATION_NO_LEAK;
    }
    if ((input->greenUv < 0) || (input->greenUv > 3600000) ||
        (input->yellowUv < 0) || (input->yellowUv > 3600000) ||
        (input->referenceDropUv < -20000) || (input->referenceDropUv > 3600000) ||
        (input->redGreenDropUv < -20000) || (input->redGreenDropUv > 3600000)) {
        return LOCATION_OUT_OF_RANGE;
    }
    referenceUv = (int64_t)input->referenceDropUv - LOCATION_REF_OFFSET_UV;
    wireUv = (int64_t)input->redGreenDropUv - LOCATION_RG_OFFSET_UV;
    if ((input->greenUv < LOCATION_GREEN_MIN_UV) || (input->yellowUv < LOCATION_YELLOW_MIN_UV)) {
        return LOCATION_UNSTABLE;
    }
    if (referenceUv < LOCATION_MIN_REF_UV) {
        return LOCATION_LOW_CURRENT;
    }
    /* 2026-11-17：同一极差不能仅看固定微伏数；相对波动过大也必须拒绝。
     * 乘法使用64位，异常UINT32_MAX输入不会溢出后误通过。 */
    if ((input->referenceSpreadUv > LOCATION_REF_SPREAD_MAX_UV) ||
        ((int64_t)input->referenceSpreadUv * 1000 > referenceUv * LOCATION_REF_SPREAD_PM) ||
        (input->redGreenSpreadUv > LOCATION_RG_SPREAD_MAX_UV)) {
        return LOCATION_UNSTABLE;
    }
    if (wireUv < 0) {
        return LOCATION_OUT_OF_RANGE;
    }
    /* 单位：uV * 欧 * 1000 / uV = 毫欧；保留毫米只表示数值分辨率。 */
    remaining = (wireUv * LOCATION_REF_OHM * 1000 + referenceUv / 2) / referenceUv;
    if (remaining > LOCATION_RG_TOTAL_MOHM) {
        return LOCATION_OUT_OF_RANGE;
    }
    distance = ((int64_t)LOCATION_LENGTH_MM * (LOCATION_RG_TOTAL_MOHM - remaining) +
                LOCATION_RG_TOTAL_MOHM / 2) / LOCATION_RG_TOTAL_MOHM;
    output.distanceMm = (int32_t)distance;
    output.remainingMilliOhm = (int32_t)remaining;
    output.currentUa = (int32_t)(referenceUv / LOCATION_REF_OHM);
    output.validated = LOCATION_VALIDATED;
    *result = output;
    return (LOCATION_VALIDATED != 0U) ? LOCATION_VALID : LOCATION_ESTIMATE;
}
