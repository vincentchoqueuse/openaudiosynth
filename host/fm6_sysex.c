/* fm6_sysex.c — voice data reader (see fm6_sysex.h), from the public data-format documentation:
 *   bulk dump:    F0 43 0n 09 20 00  <32 x 128 packed bytes>  checksum F7
 *   single voice: F0 43 0n 00 01 1B  <155 unpacked bytes>     checksum F7
 * In both layouts operator 6 comes first. Checksum = two's complement of the 7-bit sum of the data. */
#include "fm6_sysex.h"

#include <stdio.h>
#include <string.h>

static void defaults(fm6_sysex_voice_t *v)
{
    for (int i = 0; i < FM6_P_COUNT; i++)
        v->p[i] = fm6_params[i].def;
    memset(v->name, 0, sizeof v->name);
}

static float clampv(int x, int hi) { return (float)(x < 0 ? 0 : x > hi ? hi : x); }

static void set_name(fm6_sysex_voice_t *v, const uint8_t *src)
{
    for (int i = 0; i < 10; i++)
        v->name[i] = (src[i] >= 32 && src[i] < 127) ? (char)src[i] : ' ';
    v->name[10] = 0;
}

void fm6_sysex_unpack_voice(const uint8_t *b, fm6_sysex_voice_t *v)
{
    defaults(v);
    for (int o = 0; o < 6; o++) {
        const uint8_t *s = b + 17 * o;
        const int op = 5 - o; /* data starts with operator 6 */
        float *d = &v->p[FM6_OP(op, 0)];
        for (int k = 0; k < 4; k++) {
            d[FM6_R1 + k] = clampv(s[k], 99);
            d[FM6_L1 + k] = clampv(s[4 + k], 99);
        }
        d[FM6_BREAKPOINT] = clampv(s[8], 99);
        d[FM6_LDEPTH] = clampv(s[9], 99);
        d[FM6_RDEPTH] = clampv(s[10], 99);
        d[FM6_LCURVE] = clampv(s[11] & 3, 3);
        d[FM6_RCURVE] = clampv((s[11] >> 2) & 3, 3);
        d[FM6_RATE_SCALE] = clampv(s[12] & 7, 7);
        d[FM6_DETUNE] = clampv((s[12] >> 3) & 15, 14);
        d[FM6_AMP_SENS] = clampv(s[13] & 3, 3);
        d[FM6_VEL_SENS] = clampv((s[13] >> 2) & 7, 7);
        d[FM6_LEVEL] = clampv(s[14], 99);
        d[FM6_MODE] = clampv(s[15] & 1, 1);
        d[FM6_COARSE] = clampv((s[15] >> 1) & 31, 31);
        d[FM6_FINE] = clampv(s[16], 99);
    }
    const uint8_t *g = b + 102;
    v->p[FM6_P_PR1] = clampv(g[0], 99), v->p[FM6_P_PR2] = clampv(g[1], 99);
    v->p[FM6_P_PR3] = clampv(g[2], 99), v->p[FM6_P_PR4] = clampv(g[3], 99);
    v->p[FM6_P_PL1] = clampv(g[4], 99), v->p[FM6_P_PL2] = clampv(g[5], 99);
    v->p[FM6_P_PL3] = clampv(g[6], 99), v->p[FM6_P_PL4] = clampv(g[7], 99);
    v->p[FM6_P_ALGORITHM] = clampv((g[8] & 31) + 1, 32);
    v->p[FM6_P_FEEDBACK] = clampv(g[9] & 7, 7);
    v->p[FM6_P_OSC_SYNC] = clampv((g[9] >> 3) & 1, 1);
    v->p[FM6_P_LFO_SPEED] = clampv(g[10], 99);
    v->p[FM6_P_LFO_DELAY] = clampv(g[11], 99);
    v->p[FM6_P_LFO_PMD] = clampv(g[12], 99);
    v->p[FM6_P_LFO_AMD] = clampv(g[13], 99);
    v->p[FM6_P_LFO_SYNC] = clampv(g[14] & 1, 1);
    v->p[FM6_P_LFO_WAVE] = clampv((g[14] >> 1) & 7, 5);
    v->p[FM6_P_PMS] = clampv((g[14] >> 4) & 7, 7);
    v->p[FM6_P_TRANSPOSE] = clampv(g[15], 48);
    set_name(v, g + 16);
}

/* Single voice, unpacked: per operator (6 first) R1-4 L1-4 BP LD RD LC RC RS AMS KVS OL M FC FF DET. */
static void unpack_vced(const uint8_t *b, fm6_sysex_voice_t *v)
{
    defaults(v);
    for (int o = 0; o < 6; o++) {
        const uint8_t *s = b + 21 * o;
        float *d = &v->p[FM6_OP(5 - o, 0)];
        for (int k = 0; k < 4; k++) {
            d[FM6_R1 + k] = clampv(s[k], 99);
            d[FM6_L1 + k] = clampv(s[4 + k], 99);
        }
        d[FM6_BREAKPOINT] = clampv(s[8], 99), d[FM6_LDEPTH] = clampv(s[9], 99), d[FM6_RDEPTH] = clampv(s[10], 99);
        d[FM6_LCURVE] = clampv(s[11], 3), d[FM6_RCURVE] = clampv(s[12], 3), d[FM6_RATE_SCALE] = clampv(s[13], 7);
        d[FM6_AMP_SENS] = clampv(s[14], 3), d[FM6_VEL_SENS] = clampv(s[15], 7), d[FM6_LEVEL] = clampv(s[16], 99);
        d[FM6_MODE] = clampv(s[17], 1), d[FM6_COARSE] = clampv(s[18], 31), d[FM6_FINE] = clampv(s[19], 99);
        d[FM6_DETUNE] = clampv(s[20], 14);
    }
    const uint8_t *g = b + 126;
    static const int ids[] = {FM6_P_PR1, FM6_P_PR2, FM6_P_PR3, FM6_P_PR4, FM6_P_PL1, FM6_P_PL2, FM6_P_PL3, FM6_P_PL4};
    for (int k = 0; k < 8; k++)
        v->p[ids[k]] = clampv(g[k], 99);
    v->p[FM6_P_ALGORITHM] = clampv(g[8] + 1, 32);
    v->p[FM6_P_FEEDBACK] = clampv(g[9], 7);
    v->p[FM6_P_OSC_SYNC] = clampv(g[10], 1);
    v->p[FM6_P_LFO_SPEED] = clampv(g[11], 99), v->p[FM6_P_LFO_DELAY] = clampv(g[12], 99);
    v->p[FM6_P_LFO_PMD] = clampv(g[13], 99), v->p[FM6_P_LFO_AMD] = clampv(g[14], 99);
    v->p[FM6_P_LFO_SYNC] = clampv(g[15], 1), v->p[FM6_P_LFO_WAVE] = clampv(g[16], 5);
    v->p[FM6_P_PMS] = clampv(g[17], 7), v->p[FM6_P_TRANSPOSE] = clampv(g[18], 48);
    set_name(v, g + 19);
}

static int checksum_ok(const uint8_t *data, size_t n, uint8_t cs)
{
    unsigned sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += data[i];
    return ((-(int)sum) & 0x7F) == cs;
}

int fm6_sysex_parse(const uint8_t *data, size_t len, fm6_sysex_voice_t *out, int max, char *err, size_t errlen, char *warn,
              size_t warnlen)
{
    int count = 0;
    if (warn && warnlen)
        warn[0] = 0;
    for (size_t i = 0; i + 6 < len; i++) {
        if (data[i] != 0xF0 || data[i + 1] != 0x43)
            continue;
        const uint8_t format = data[i + 3];
        const size_t size = ((size_t)data[i + 4] << 7) | data[i + 5];
        const uint8_t *payload = data + i + 6;
        if (i + 6 + size + 2 > len)
            break;
        const int ok = checksum_ok(payload, size, payload[size]);
        if (!ok && warn) {
            const size_t u = strlen(warn);
            snprintf(warn + u, warnlen - u, "checksum mismatch in message at byte %zu (data kept)\n", i);
        }
        if (format == 9 && size == 4096) {
            for (int k = 0; k < 32 && count < max; k++)
                fm6_sysex_unpack_voice(payload + 128 * k, &out[count++]);
        } else if (format == 0 && size == 155 && count < max) {
            unpack_vced(payload, &out[count++]);
        }
        i += 6 + size + 1;
    }
    if (count == 0)
        snprintf(err, errlen, "no 32-voice bulk dump or single-voice message found");
    return count ? count : -1;
}
