#ifndef APP_LEAK_H
#define APP_LEAK_H
#include <stdint.h>

/* 2026-11-17：短接释放后黄线可残留约0.4V，放宽至0.5V并连续2轮恢复。
 * 仍要求绿线>=2.5V且压差>=2V；中间区和采样故障不会消警。 */
/* 2026-11-17：覆盖持续浸水黄线约1.2~1.7V的弱导通日志。
 * 绿线>=2V、黄线>=0.80V连续3轮报警，不要求两路电压接近。
 * 黄线<=0.50V且满足原绿线/压差条件连续2轮恢复，保留中间缓冲区。
 * 当前为浸水实验门限；定位独立保留严格条件，未标定不同水质。
 * 电压单位uV，确认计数为完整新采样轮数，不是毫秒。 */
#define LEAK_LOOP_MIN_UV       2000000
#define LEAK_LOOP_MAX_DIFF_UV   250000
#define LEAK_WET_GREEN_MIN_UV  2000000
#define LEAK_WET_YELLOW_MIN_UV  800000
#define LEAK_DRY_GREEN_MIN_UV  2500000
#define LEAK_DRY_YELLOW_MAX_UV  500000
#define LEAK_DRY_MIN_DIFF_UV   2000000
#define LEAK_WET_CONFIRM       3U
#define LEAK_DRY_CONFIRM       2U
#define LEAK_BREAK_CONFIRM     3U
#define LEAK_FAULT_RECOVER     3U
#define LEAK_VALID_ALL         0x07U

typedef enum {
    LEAK_CHECKING = 0,
    LEAK_NORMAL,
    LEAK_DETECTED,
    LEAK_CABLE_BREAK,
    LEAK_SENSOR_FAULT
} LeakState;

typedef struct {
    int32_t yellowSourceUv;
    int32_t blackReturnUv;
    int32_t redSourceUv;
    int32_t greenReturnUv;
    int32_t wetGreenUv;
    int32_t wetYellowUv;
    /* 位0=黄黑、位1=红绿、位2=浸水采样，只有本轮成功且未削顶才置1。 */
    uint8_t validMask;
} LeakFrame;

/* 主循环初始化纯状态机，不访问GPIO、ADC、屏幕或蜂鸣器。 */
void app_leak_init(void);
/* 每取得一轮三组新数据调用一次；frame仅在调用期间有效，不保存指针。
 * NULL、无效位、超出合理电压范围立即FAULT；不会当作干燥消警。
 * 未确认的导通/中间电压为CHECK；已确认LEAK在中间区保持。
 * 回路异常优先于浸水判断，故障期间保留之前确认的浸水锁存。
 * 仅限主循环，不可并发；支持检测信号断线，不保证覆盖任意接线故障。 */
void app_leak_update(const LeakFrame *frame);
LeakState app_leak_state(void);
/* 返回静态只读状态文字，调用者不得修改或释放。 */
const char *app_leak_name(void);
/* 浸水锁存、已确认回路异常、采样故障任一成立时返回1，否则0。 */
uint8_t app_leak_alarm(void);
#endif
