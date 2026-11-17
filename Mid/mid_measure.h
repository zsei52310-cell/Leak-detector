#ifndef MID_MEASURE_H
#define MID_MEASURE_H
#include <stdint.h>

typedef enum {
    MEASURE_REFERENCE = 0,
    MEASURE_YELLOW_BLACK,
    MEASURE_GREEN_YELLOW,
    MEASURE_RED_GREEN,
    MEASURE_WATER /* 红供电/黑回流，AIN0=绿，AIN1=黄 */
} MeasureRoute;

typedef enum {
    MEASURE_OK = 0,
    MEASURE_ARGUMENT,
    MEASURE_NOT_READY,
    MEASURE_SWITCH_ERROR,
    MEASURE_WAIT_ERROR,
    MEASURE_ADC_ERROR,
    MEASURE_SATURATED
} MeasureStatus;

typedef struct {
    int32_t ain0Uv;
    int32_t ain1Uv;
    int32_t differenceUv;
    int16_t ain0Raw;
    int16_t ain1Raw;
} MeasureSample;

/* 主循环初始化开关，初始全关并选择参考通道；ADC需另行初始化。
 * 这是硬件诊断阶段的测量层，尚不判断漏水、断线或位置。 */
void mid_measure_init(void);
/* 同步诊断流程：全关->1ms死区->选通道->开激励->稳定10ms->两路ADC->全关。
 * sample不可为空且不保留，仅MEASURE_OK才更新；两路依次采样，非同时采样。
 * 成功、失败、参数错误都关闭4066，采样超时由ADC有限轮询保证。
 * 独占相关开关和ADC，不可在ISR或并发调用。参考通道无激励。
 * 不接线缆时参考上下端都约0V，不能据此验证参考电阻阻值。
 * WATER用于已确认的远端红绿、黄黑两组连接；仅采电压，不判浸水。
 * 绿黑裸露点连接是强导通模拟；若有真正断线，应先修复导体连续性。 */
MeasureStatus mid_measure_read(MeasureRoute route, MeasureSample *sample);
/* 最近一次读ADC时的底层状态码，0为SGM_OK，仅供诊断输出。 */
uint8_t mid_measure_fault(void);
typedef struct {
    MeasureSample greenYellow;
    MeasureSample reference;
    MeasureSample redGreen;
    uint32_t greenYellowSpreadUv;
    uint32_t referenceSpreadUv;
    uint32_t redGreenSpreadUv;
} LocateSample;

/* 2026-11-17：红供电/黑回流保持不变，依次采绿黄、参考上下端、红绿。
 * 各组切换后等待10ms，每组采4对求平均并记录差分极差；电压依次采集，不是同一时刻的快照。
 * sample不可为空且不保留；仅MEASURE_OK时整体更新。
 * 成功或任何错误都会关闭4066；不计算电流、距离或更改报警状态。
 * 主循环同步调用，独占模拟开关和ADC；测试点需在一组采样期间保持不动。 */
MeasureStatus mid_measure_locate(LocateSample *sample);
#endif
