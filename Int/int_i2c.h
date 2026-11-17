#ifndef INT_I2C_H
#define INT_I2C_H
#include <stddef.h>
#include <stdint.h>

/* OLED专用软件I2C：PB1=SCL，PB2=SDA，开漏输出，需要外部上拉至3.3V。
 * 主循环调用，不可与其他调用者并发；返回1成功、0失败。 */
uint8_t i2c_init(void);
/* address为7位地址；data仅在length为0时允许NULL，此时仅探测地址。
 * 指针只在调用期间使用。逐字节检查ACK，失败时可能已发送部分数据。 */
uint8_t i2c_write(uint8_t address, const uint8_t *data, size_t length);
#endif
