/* denormal.h — enables flush-to-zero (denormal numbers set to zero) for the current thread.
 * Apple Silicon: FZ bit (24) of the FPCR register. Cortex-M7: FZ bit (24) of FPSCR.
 * The setting is per thread: synth_render calls it on every entry (cost: 2 instructions). */
#ifndef DENORMAL_H
#define DENORMAL_H

#include <stdint.h>

/* STM32 note: inside an interrupt, FPSCR is reloaded from FPU->FPDSCR.
 * The board code must therefore also do FPU->FPDSCR |= FPU_FPDSCR_FZ_Msk at init. */
static inline void dsp_denormals_off(void)
{
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (uint64_t)1 << 24;
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__arm__) && defined(__ARM_FP)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (uint32_t)1 << 24;
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fpscr));
#elif defined(__x86_64__) || defined(__SSE__)
    uint32_t csr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040u; /* FTZ + DAZ */
    __asm__ __volatile__("ldmxcsr %0" : : "m"(csr));
#endif
}

#endif /* DENORMAL_H */
