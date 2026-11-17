#ifndef APP_LOCATION_H
#define APP_LOCATION_H
#include <stdint.h>

/* 2026-11-17：本板单点导通实验参数，不能将标称值视为已验证精度。
 * 红绿总回路含引线/接点，暂按实测12.4欧；长度960mm。
 * 两个偏移为AIN0-AIN1差分零点，不是绝对电压偏移，必须实测再改。 */
#define LOCATION_LENGTH_MM      960
#define LOCATION_RG_TOTAL_MOHM  12400
#define LOCATION_REF_OHM        3900
#define LOCATION_RG_OFFSET_UV   0
#define LOCATION_REF_OFFSET_UV  0
/* 至少经过多位置验证、阻值和偏移标定后才能改为1。 */
#define LOCATION_VALIDATED      0U
/* 2026-11-17：真实水阻使绿黄电压不相等，取消原短接压差门槛。
 * 定位仍独立检查有导通、参考电流、采样波动和阻值范围。
 * 单点串联模型按同次参考电流换算线阻，水阻不直接当成距离。 */
#define LOCATION_GREEN_MIN_UV   2000000
#define LOCATION_YELLOW_MIN_UV   800000
#define LOCATION_MIN_REF_UV      100000
/* 2026-11-17：相对极差与绝对上限同时满足，避免弱电流仍沿用固定20mV门槛。
 * 均为实验限值；复测防止跨越湿干阶段，不代表已完成距离精度标定。 */
#define LOCATION_REF_SPREAD_PM       30U
#define LOCATION_REF_SPREAD_MAX_UV 50000U
#define LOCATION_RG_SPREAD_MAX_UV    500U
#define LOCATION_RECHECK_MAX_UV   100000

typedef enum {
    LOCATION_ESTIMATE = 0,
    LOCATION_VALID,
    LOCATION_ARGUMENT,
    LOCATION_NO_LEAK,
    LOCATION_UNSTABLE,
    LOCATION_LOW_CURRENT,
    LOCATION_OUT_OF_RANGE
} LocationStatus;

typedef struct {
    int32_t greenUv;
    int32_t yellowUv;
    int32_t referenceDropUv;
    int32_t redGreenDropUv;
    uint32_t referenceSpreadUv;
    uint32_t redGreenSpreadUv;
    uint8_t leakConfirmed;
} LocationInput;

typedef struct {
    int32_t distanceMm;
    int32_t remainingMilliOhm;
    int32_t currentUa;
    uint8_t validated;
} LocationResult;

/* 纯计算，主循环调用；指针不可为空、不保留。
 * 仅ESTIMATE或VALID更新result，其他状态保留原输出，调用者必须检查状态。
 * 模型：单点稳定导通、红/黄信号线电阻很小、绿感应线电阻均匀。
 * I=参考压降/Rref；R剩余=红绿压降/I；x=L*(1-R剩余/R整段)。
 * 尚未消除引线/接点与ADC系统偏移，不直接套用资料中编号矛盾的公式。
 * 不要求绿黄等电位；弱导通时电流更小，误差可能更明显。
 * 整段/多点浸水不满足单点模型，输出仅为实验候选位置。
 * 不输出定位精度，超范围不裁剪成0或L。 */
LocationStatus app_location_calc(const LocationInput *input, LocationResult *result);
#endif
