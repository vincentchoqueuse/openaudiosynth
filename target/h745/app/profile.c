/* profile.c — sampling profiler of the M7 (see profile.h). TIM7 is a basic timer CubeMX does not use; its
 * interrupt handler is naked so that the MSP at entry points at the exception frame, whose 7th word is the
 * interrupted PC. */
#include "profile.h"

#include "main.h"

uint32_t profile_pc[PROFILE_MAX] __attribute__((section(".RAM_D1"), aligned(32)));
volatile uint32_t profile_count;

void profile_sample(const uint32_t *frame);

void profile_sample(const uint32_t *frame)
{
    TIM7->SR = 0;
    if (profile_count < PROFILE_MAX)
        profile_pc[profile_count++] = frame[6];
}

__attribute__((naked)) void TIM7_IRQHandler(void)
{
    __asm volatile("mrs r0, msp\n"
                   "b profile_sample\n");
}

void profile_start(uint32_t rate_hz)
{
    profile_count = 0;
    __HAL_RCC_TIM7_CLK_ENABLE();
    const uint32_t clk = 2u * HAL_RCC_GetPCLK1Freq(); /* timers on APB1 run at twice its clock */
    TIM7->CR1 = 0;
    TIM7->PSC = 0;
    TIM7->ARR = clk / rate_hz - 1u;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0;
    TIM7->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM7_IRQn, 15, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    TIM7->CR1 = TIM_CR1_CEN;
}

void profile_stop(void)
{
    TIM7->CR1 = 0;
    HAL_NVIC_DisableIRQ(TIM7_IRQn);
    SCB_CleanDCache_by_Addr(profile_pc, (int32_t)sizeof(profile_pc)); /* the debugger reads the memory */
}
