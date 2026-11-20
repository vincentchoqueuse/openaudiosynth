/* audio.c — audio output of the STM32H745I-DISCO (see audio.h), on ST's board support package
 * (stm32h745i_discovery_audio.c: WM8994 setup over I2C4, SAI2 block A master, DMA2 stream 1 circular).
 * The DMA buffer lives in the AXI SRAM (the DMA cannot reach the DTCM) and is cleaned from the D-cache
 * after each render. */
#include "audio.h"

#include "main.h"
#include "stm32h745i_discovery_audio.h"

static int32_t audio_buf[2 * AUDIO_FRAMES * 2] __attribute__((section(".RAM_D1"), aligned(32)));
static float audio_l[AUDIO_FRAMES], audio_r[AUDIO_FRAMES];
static audio_render_fn audio_render;
static uint32_t audio_cycles_avail;
static volatile float audio_avg, audio_peak;
static volatile uint32_t audio_overrun_count, audio_dma_errors, audio_streak;
static volatile uint32_t audio_count; /* buffers rendered: the interrupt is alive (watchdog) */

static void audio_fill(int32_t *out)
{
    const uint32_t t0 = DWT->CYCCNT;
    audio_count++;
    audio_render(audio_l, audio_r, AUDIO_FRAMES);
    for (int i = 0; i < AUDIO_FRAMES; i++) {
        /* clamp, then scale to the largest float below 2^31 so the conversion never overflows */
        float l = audio_l[i], r = audio_r[i];
        if (l != l) /* NaN: silence rather than a full-scale click (synth.c also guards each voice) */
            l = 0.0f;
        if (r != r)
            r = 0.0f;
        l = l > 1.0f ? 1.0f : (l < -1.0f ? -1.0f : l);
        r = r > 1.0f ? 1.0f : (r < -1.0f ? -1.0f : r);
        out[2 * i] = (int32_t)(l * 2147483520.0f);
        out[2 * i + 1] = (int32_t)(r * 2147483520.0f);
    }
    SCB_CleanDCache_by_Addr((uint32_t *)out, (int32_t)(AUDIO_FRAMES * 2 * sizeof(int32_t)));
    const float load = (float)(DWT->CYCCNT - t0) / (float)audio_cycles_avail;
    audio_avg += 0.01f * (load - audio_avg);
    if (load > audio_peak)
        audio_peak = load;
    if (load >= 1.0f) { /* the DMA reached this half before it was written: one buffer of stale sound */
        audio_overrun_count++;
        audio_streak++;
    } else {
        audio_streak = 0;
    }
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(uint32_t instance)
{
    (void)instance;
    audio_fill(audio_buf);
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(uint32_t instance)
{
    (void)instance;
    audio_fill(audio_buf + AUDIO_FRAMES * 2);
}

void BSP_AUDIO_OUT_Error_CallBack(uint32_t instance)
{
    (void)instance;
    audio_dma_errors++;
}

/* DMA2 stream 1 carries SAI2 block A (AUDIO_OUT_SAIx_DMAx_IRQHandler in the BSP) */
void DMA2_Stream1_IRQHandler(void)
{
    BSP_AUDIO_OUT_IRQHandler(0);
}

int audio_start(audio_render_fn render, int volume)
{
    audio_render = render;

    /* cycle counter for the load measurement */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    audio_cycles_avail = SystemCoreClock / AUDIO_SAMPLE_RATE * AUDIO_FRAMES;

    /* silence until the first interrupt */
    for (int i = 0; i < 2 * AUDIO_FRAMES * 2; i++)
        audio_buf[i] = 0;
    SCB_CleanDCache_by_Addr((uint32_t *)audio_buf, (int32_t)sizeof(audio_buf));

    /* CubeMX's HAL_SAI_MspInit (CM7/Core/Src/sai.c) would switch the SAI2 kernel clock back to PLL1 when
     * HAL_SAI_Init sees a fresh handle. The BSP sets up the pins, the DMA and the audio PLL itself, so the
     * handle is marked as already initialized. */
    haudio_out_sai.State = HAL_SAI_STATE_READY;

    BSP_AUDIO_Init_t init = {
        .Device = AUDIO_OUT_DEVICE_HEADPHONE,
        .SampleRate = AUDIO_FREQUENCY_48K,
        .BitsPerSample = AUDIO_RESOLUTION_32B,
        .ChannelsNbr = 2,
        .Volume = (uint32_t)volume,
    };
    int32_t ret = BSP_AUDIO_OUT_Init(0, &init);
    /* the sample rate is only right if the SAI runs on the audio PLL set up by the BSP (PLL2, 49.142 MHz
     * for the 48 kHz family): refuse to start otherwise rather than play out of tune */
    if (ret == BSP_ERROR_NONE && __HAL_RCC_GET_SAI2_SOURCE() != RCC_SAI2CLKSOURCE_PLL2)
        ret = AUDIO_ERROR_CLOCK;
    if (ret == BSP_ERROR_NONE)
        ret = BSP_AUDIO_OUT_Play(0, (uint8_t *)audio_buf, sizeof(audio_buf));
    return (int)ret;
}

float audio_load_avg(void) { return audio_avg; }
float audio_load_peak(void) { return audio_peak; }
void audio_load_peak_reset(void) { audio_peak = 0.0f; }
uint32_t audio_overruns(void) { return audio_overrun_count; }
uint32_t audio_overrun_streak(void) { return audio_streak; }
uint32_t audio_errors(void) { return audio_dma_errors; }

uint32_t audio_buffers(void) { return audio_count; }

void audio_silence(void)
{
    for (unsigned i = 0; i < sizeof audio_buf / sizeof audio_buf[0]; i++)
        audio_buf[i] = 0;
    SCB_CleanDCache_by_Addr((uint32_t *)audio_buf, (int32_t)sizeof audio_buf);
}
