/* mt48lc4m32b2_conf.h — configuration of ST's SDRAM driver for the STM32H745I-DISCO: refresh counter for a
 * 100 MHz SDRAM clock (HCLK 200 MHz / 2, values of ST's board demonstration). */
#ifndef MT48LC4M32B2_CONF_H
#define MT48LC4M32B2_CONF_H
#include "stm32h7xx_hal.h"
#define REFRESH_COUNT ((uint32_t)0x0603)
#define MT48LC4M32B2_TIMEOUT ((uint32_t)0xFFFF)
#endif /* MT48LC4M32B2_CONF_H */
