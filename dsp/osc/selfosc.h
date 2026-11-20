/* selfosc.h — self-oscillating nonlinear oscillator (Van der Pol with a Duffing term), forced by a sine at the
 * note frequency:  x'' - mu (1 - x^2) x' + x + beta x^3 = F cos(phi_note), in time scaled by the natural angular
 * frequency. mu (drive) morphs a pure sine (mu ~ 0) into a relaxation wave (square / pulse-like, mu ~ 5);
 * beta (stiffness) makes the pitch depend on the amplitude; F (entrain) locks the oscillator onto the note.
 * The pitch drop of the Van der Pol cycle with mu is compensated by a precomputed table. Integrated with
 * an exact damping step between two half kicks (Strang splitting, leapfrog) at 4x the sample rate, decimated by two half-band stages.
 * References: B. van der Pol (1926); A. Pikovsky, M. Rosenblum, J. Kurths, "Synchronization" (2001). */
#ifndef SELFOSC_H
#define SELFOSC_H

#include "../filter/halfband.h"

#define SELFOSC_MU_MAX 5.0f
#define SELFOSC_TABLE 21 /* compensation table: mu = 0, 0.25 .. 5 */

typedef struct {
    float ratio[SELFOSC_TABLE]; /* frequency of the free cycle / natural frequency, by mu */
} selfosc_table_t;

typedef struct {
    float x, v;    /* state */
    float fph;     /* phase of the forcing (cycles) */
    hb2x_t d1, d2; /* 4x -> 2x -> 1x */
} selfosc_t;

/* Measures the pitch of the free Van der Pol cycle for every table entry: 1.26 million integration steps, too
 * slow for an engine init on the board. It is the generator of selfosc_table (and its test), not runtime code. */
void selfosc_table_init(selfosc_table_t *t);
/* The table selfosc_table_init computes (it depends on nothing: neither the sample rate nor the instance). */
extern const selfosc_table_t selfosc_table;
/* Frequency of the free cycle relative to the natural frequency, for drive mu (interpolated). */
float selfosc_ratio(const selfosc_table_t *t, float mu);

void selfosc_reset(selfosc_t *s, float x0);
/* Adds nothing: writes n samples. f_nat: natural frequency (Hz); f_force: forcing frequency (Hz); mu: drive
 * 0..5; beta: stiffness; force: forcing amplitude; out: about +/-1. */
void selfosc_process(selfosc_t *s, float sample_rate, float f_nat, float f_force, float mu, float beta, float force,
                     float *out, int n);

#endif /* SELFOSC_H */
