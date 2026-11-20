/* kit.c — reading and writing the directory of a sample kit (kit.h): every field read byte by byte, every
 * sample checked to lie inside the kit, so a damaged kit is refused instead of read out of bounds. */
#include "kit.h"

static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int kit_count(const uint8_t *kit, size_t size)
{
    if (!kit || size < KIT_HEADER || get32(kit) != KIT_MAGIC || get16(kit + 4) != KIT_VERSION)
        return -1;
    const uint32_t n = get16(kit + 6);
    if (n > KIT_MAX || size < KIT_HEADER + (size_t)n * KIT_ENTRY)
        return -1;
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *e = kit + KIT_HEADER + k * KIT_ENTRY;
        const uint32_t off = get32(e + 24), frames = get32(e + 28);
        if (e[20] > 127 || (off & 1u) || off < KIT_HEADER + n * KIT_ENTRY || off > size ||
            frames > (size - off) / 2u)
            return -1;
    }
    return (int)n;
}

void kit_entry(const uint8_t *kit, int k, kit_entry_t *e)
{
    const uint8_t *p = kit + KIT_HEADER + k * KIT_ENTRY;
    for (int i = 0; i < KIT_NAME; i++)
        e->name[i] = (char)p[i];
    e->name[KIT_NAME - 1] = 0;
    e->note = p[20];
    e->flags = p[21];
    e->offset = get32(p + 24);
    e->frames = get32(p + 28);
}

int kit_write_directory(uint8_t *kit, const kit_entry_t *e, int count)
{
    if (count < 0 || count > KIT_MAX)
        return -1;
    put32(kit, KIT_MAGIC);
    put16(kit + 4, KIT_VERSION);
    put16(kit + 6, (uint32_t)count);
    for (int k = 0; k < count; k++) {
        uint8_t *p = kit + KIT_HEADER + k * KIT_ENTRY;
        int i = 0;
        for (; i < KIT_NAME - 1 && e[k].name[i]; i++)
            p[i] = (uint8_t)e[k].name[i];
        for (; i < KIT_NAME; i++)
            p[i] = 0;
        p[20] = e[k].note;
        p[21] = e[k].flags;
        put16(p + 22, 0);
        put32(p + 24, e[k].offset);
        put32(p + 28, e[k].frames);
    }
    return KIT_HEADER + count * KIT_ENTRY;
}
