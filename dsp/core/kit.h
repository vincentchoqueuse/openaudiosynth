/* kit.h — a sample kit in memory: the samples the sampler engine plays, read where they lie (flash, SDRAM,
 * a file loaded by the host) without being copied. Format "OIDK" version 1, little-endian:
 *   header (8 bytes)    "OIDK", version u16, count u16 (at most KIT_MAX)
 *   directory           count entries of 32 bytes {name[20], note u8, flags u8, reserved u16, offset u32, frames u32}
 *   samples             16-bit signed mono PCM at 48 kHz, each at its offset (bytes from the start of the kit, even)
 * One sample per MIDI note. Transported to the boards by the board link (docs/link.md). */
#ifndef KIT_H
#define KIT_H

#include <stddef.h>
#include <stdint.h>

#define KIT_MAGIC 0x4B44494Fu /* "OIDK" read as a little-endian u32 */
#define KIT_VERSION 1u
#define KIT_HEADER 8
#define KIT_ENTRY 32
#define KIT_MAX 128
#define KIT_NAME 20
#define KIT_RATE 48000 /* sample rate of the samples, Hz */

typedef struct {
    char name[KIT_NAME];
    uint8_t note;    /* MIDI note that plays it */
    uint8_t flags;   /* reserved, 0 */
    uint32_t offset; /* bytes from the start of the kit */
    uint32_t frames; /* 16-bit samples */
} kit_entry_t;

/* Checks a kit: magic, version, every sample inside it. Returns its number of samples, -1 if invalid. */
int kit_count(const uint8_t *kit, size_t size);
/* Entry k of a valid kit. */
void kit_entry(const uint8_t *kit, int k, kit_entry_t *e);
/* The samples of an entry (the kit must be 2-byte aligned; the PCM is little-endian like every target). */
static inline const int16_t *kit_samples(const uint8_t *kit, const kit_entry_t *e)
{
    return (const int16_t *)(const void *)(kit + e->offset);
}
/* Writes the header and the directory (KIT_HEADER + count * KIT_ENTRY bytes); the caller writes the samples
 * at the entries' offsets. Returns the directory's size, -1 if count is out of range. */
int kit_write_directory(uint8_t *kit, const kit_entry_t *e, int count);

#endif /* KIT_H */
