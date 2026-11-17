#include "app_leak.h"
#include <stddef.h>

static LeakState g_state = LEAK_CHECKING;
static uint8_t g_wetCount = 0U;
static uint8_t g_dryCount = 0U;
static uint8_t g_breakCount = 0U;
static uint8_t g_goodCount = 0U;
static uint8_t g_leakLatched = 0U;

/* 2026-11-16：饱和计数，不让长期运行的uint8_t确认计数回绕。 */
static uint8_t leak_count(uint8_t value, uint8_t limit)
{
    return (value < limit) ? (uint8_t)(value + 1U) : limit;
}

/* 2026-11-16：先限制电压范围，之后相减不会溢出int32_t。 */
static uint8_t leak_voltage(int32_t voltageUv)
{
    return (uint8_t)((voltageUv >= -20000) && (voltageUv <= 3600000));
}

/* 2026-11-16：检查终端回路电源与回流电压、差值，阈值待线缆标定。 */
static uint8_t leak_loop(int32_t sourceUv, int32_t returnUv)
{
    int32_t difference = sourceUv - returnUv;
    if (difference < 0) {
        difference = -difference;
    }
    return (uint8_t)((sourceUv >= LEAK_LOOP_MIN_UV) &&
                     (returnUv >= LEAK_LOOP_MIN_UV) &&
                     (difference <= LEAK_LOOP_MAX_DIFF_UV));
}

/* 2026-11-16：复位所有确认历史，上电不假定线缆正常。 */
void app_leak_init(void)
{
    g_state = LEAK_CHECKING;
    g_wetCount = 0U;
    g_dryCount = 0U;
    g_breakCount = 0U;
    g_goodCount = 0U;
    g_leakLatched = 0U;
}

/* 2026-11-16：完整新帧推进一次；故障与回路检查先于浸水判断。 */
void app_leak_update(const LeakFrame *frame)
{
    int32_t difference;
    uint8_t wet;
    uint8_t dry;
    if ((frame == NULL) || (frame->validMask != LEAK_VALID_ALL) ||
        (leak_voltage(frame->yellowSourceUv) == 0U) ||
        (leak_voltage(frame->blackReturnUv) == 0U) ||
        (leak_voltage(frame->redSourceUv) == 0U) ||
        (leak_voltage(frame->greenReturnUv) == 0U) ||
        (leak_voltage(frame->wetGreenUv) == 0U) ||
        (leak_voltage(frame->wetYellowUv) == 0U)) {
        g_state = LEAK_SENSOR_FAULT;
        g_wetCount = 0U;
        g_dryCount = 0U;
        g_breakCount = 0U;
        g_goodCount = 0U;
        return;
    }
    if ((leak_loop(frame->yellowSourceUv, frame->blackReturnUv) == 0U) ||
        (leak_loop(frame->redSourceUv, frame->greenReturnUv) == 0U)) {
        g_wetCount = 0U;
        g_dryCount = 0U;
        g_goodCount = 0U;
        g_breakCount = leak_count(g_breakCount, LEAK_BREAK_CONFIRM);
        if (g_breakCount >= LEAK_BREAK_CONFIRM) {
            g_state = LEAK_CABLE_BREAK;
        } else if ((g_state != LEAK_SENSOR_FAULT) && (g_state != LEAK_CABLE_BREAK)) {
            g_state = (g_leakLatched != 0U) ? LEAK_DETECTED : LEAK_CHECKING;
        }
        return;
    }
    g_breakCount = 0U;
    g_goodCount = leak_count(g_goodCount, LEAK_FAULT_RECOVER);
    if (((g_state == LEAK_SENSOR_FAULT) || (g_state == LEAK_CABLE_BREAK)) &&
        (g_goodCount < LEAK_FAULT_RECOVER)) {
        return;
    }

    difference = frame->wetGreenUv - frame->wetYellowUv;
    /* 2026-11-17：弱导通时两路可相差约2V，不能用短接压差限制漏水报警。
     * 先前的完整帧、物理范围及回路检查仍然优先执行。 */
    wet = (uint8_t)((frame->wetGreenUv >= LEAK_WET_GREEN_MIN_UV) &&
                    (frame->wetYellowUv >= LEAK_WET_YELLOW_MIN_UV));
    dry = (uint8_t)((frame->wetGreenUv >= LEAK_DRY_GREEN_MIN_UV) &&
                    (frame->wetYellowUv <= LEAK_DRY_YELLOW_MAX_UV) &&
                    (difference >= LEAK_DRY_MIN_DIFF_UV));
    /* 进入与退出用不同门限；中间区不当作正常，不推进消警计数。 */
    if (wet != 0U) {
        g_wetCount = leak_count(g_wetCount, LEAK_WET_CONFIRM);
        g_dryCount = 0U;
        if (g_wetCount >= LEAK_WET_CONFIRM) {
            g_leakLatched = 1U;
        }
    } else if (dry != 0U) {
        g_dryCount = leak_count(g_dryCount, LEAK_DRY_CONFIRM);
        g_wetCount = 0U;
        if (g_dryCount >= LEAK_DRY_CONFIRM) {
            g_leakLatched = 0U;
        }
    } else {
        g_wetCount = 0U;
        g_dryCount = 0U;
    }
    if (g_leakLatched != 0U) {
        g_state = LEAK_DETECTED;
    } else if (g_dryCount >= LEAK_DRY_CONFIRM) {
        g_state = LEAK_NORMAL;
    } else {
        g_state = LEAK_CHECKING;
    }
}

/* 2026-11-16：供显示和日志读取已确认的状态，不操作硬件。 */
LeakState app_leak_state(void)
{
    return g_state;
}

/* 2026-11-16：为屏幕及日志提供一致的状态名称。 */
const char *app_leak_name(void)
{
    switch (g_state) {
        case LEAK_CHECKING: return "CHECK";
        case LEAK_NORMAL: return "NORMAL";
        case LEAK_DETECTED: return "LEAK";
        case LEAK_CABLE_BREAK: return "BREAK";
        case LEAK_SENSOR_FAULT: return "FAULT";
        default: return "FAULT";
    }
}

/* 2026-11-16：测量故障不撤销此前的浸水锁存，故障本身也要求提示。 */
uint8_t app_leak_alarm(void)
{
    return (uint8_t)((g_leakLatched != 0U) || (g_state == LEAK_CABLE_BREAK) ||
                    (g_state == LEAK_SENSOR_FAULT));
}
