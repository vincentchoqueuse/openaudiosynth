/* app_m4.h — the M4 application of the STM32H745I-DISCO: the touch screen (preset selector and status of
 * the M7). Called from CubeMX's CM4 main.c, inside its USER CODE blocks only. */
#ifndef APP_M4_H
#define APP_M4_H

void app_m4_init(void); /* after HAL_Init and the generated peripherals: mailbox, SDRAM, display */
void app_m4_loop(void); /* body of the main loop: touch, requests to the M7, screen updates */

#endif /* APP_M4_H */
