#ifndef INT_ADC_I2C_H
#define INT_ADC_I2C_H
#include <stdint.h>

/* ADC独立总线PB8=SCL/PB9=SDA，外部4.7k上拉至3.3V。
 * 以下函数仅供主循环使用，不可并发；返回1成功、0失败。 */
uint8_t adc_i2c_init(void);
uint8_t adc_i2c_write16(uint8_t address, uint8_t reg, uint16_t value);
/* value不可为空，返回1时才更新；仅调用期间使用指针。 */
uint8_t adc_i2c_read16(uint8_t address, uint8_t reg, uint16_t *value);
/* 测试用有限阻塞等待，1~1000ms；DWT计数异常时返回0。 */
uint8_t adc_i2c_wait(uint32_t durationMs);
/* 只探测地址应答，不读写寄存器；地址为7位。 */
uint8_t adc_i2c_probe(uint8_t address);
#endif
