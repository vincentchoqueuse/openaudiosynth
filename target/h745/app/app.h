/* app.h — the M7 application of the STM32H745I-DISCO: called from CubeMX's main.c, inside its USER CODE
 * blocks only, so the project can be regenerated. */
#ifndef APP_H
#define APP_H

void app_boot(void); /* first thing after the clock setup, before the M4 is released */
void app_init(void); /* after the clock and the peripherals: caches, synth, audio output */
void app_loop(void); /* body of the main loop */
void app_fault(void); /* from the fault handlers: silence, then wait for the watchdog to reset the board */

#endif /* APP_H */
