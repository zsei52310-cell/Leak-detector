#ifndef INT_SGM58031_H
#define INT_SGM58031_H
#include <stdint.h>

typedef enum {
    SGM_OK = 0,
    SGM_ARGUMENT,
    SGM_NOT_READY,
    SGM_BUS_ERROR,
    SGM_CONFIG_ERROR,
    SGM_BUSY,
    SGM_TIMEOUT
} SgmStatus;

typedef struct {
    int16_t raw;
    int32_t microvolts;
    uint8_t saturated;
} SgmSample;

/* 主循环初始化：固定地址0x48，内部2.048V基准、量程±4.096V、100SPS。
 * 比较器禁用；不驱动模拟开关，不启用外部基准。 */
SgmStatus sgm58031_init(void);
/* id不可为空、仅调用期间使用，SGM_OK时有效；不把单次ACK当作身份验证。 */
SgmStatus sgm58031_id(uint16_t *id);
/* 主循环阻塞式单次采样，通道0~3对应AIN0~AIN3对GND。
 * sample不可为空且不被保存，仅SGM_OK时更新；可有小负偏移。
 * 最多轮询100次、间隔1ms，各I2C阶段另有有限超时。
 * 满量程时仍返回SGM_OK，saturated=1提醒数据已削顶。
 * AIN2在本板未接；AIN3已接TL431，只供本次基准检查使用。
 * 不可在ISR调用或并发调用，量程不代表引脚能承受超过VDD的电压。 */
SgmStatus sgm58031_read(uint8_t channel, SgmSample *sample);
/* 初始化诊断步骤：0完成，1总线，2断电命令，3等待，4Config1写，
 * 5Config1读，6Config1校验，7Config写，8Config读，9Config校验。
 * 仅返回最近一次初始化停留步骤，不访问硬件。 */
uint8_t sgm58031_step(void);
#endif
