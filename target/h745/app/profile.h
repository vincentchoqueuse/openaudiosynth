/* profile.h — a sampling profiler for the M7: TIM7 interrupts the code at a fixed rate and records the
 * address that was running (the PC saved in the exception frame). tools/h745_bench.py --profile reads the
 * samples through the ST-LINK and turns them into functions and source lines (arm-none-eabi-addr2line).
 * Used by the on-board benchmark only, with the audio interrupt stopped: the main loop runs on the MSP. */
#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>

#define PROFILE_MAX 16384

extern uint32_t profile_pc[PROFILE_MAX]; /* in the AXI SRAM: cleaned from the D-cache by profile_stop */
extern volatile uint32_t profile_count;

void profile_start(uint32_t rate_hz);
void profile_stop(void);

#endif /* PROFILE_H */
