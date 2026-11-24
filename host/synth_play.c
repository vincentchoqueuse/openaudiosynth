/* synth_play.c — real-time player for macOS: CoreAudio output unit + CoreMIDI input (all sources,
 * hot-plug aware, e.g. Arturia MiniLab 3 over USB) + computer-keyboard fallback.
 * Threads: CoreMIDI and the UI push messages into lock-free SPSC rings; only the audio thread
 * calls the synth_* API. Apple frameworks only (CoreAudio, AudioToolbox, CoreMIDI). */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "engines/va.h"
#include "mod/params.h"
#include "engines/engine.h"
#include "catalog.h"
#include "json.h"
#include "synth.h"

/* ---------------- lock-free single-producer / single-consumer ring ---------------- */

enum { MSG_MIDI = 0, MSG_PARAM, MSG_ENGINE };

typedef struct {
    uint8_t kind, len;
    uint8_t data[3];
    int id;
    float value;
} host_msg_t;

#define RING_SIZE 1024 /* power of two */
typedef struct {
    host_msg_t buf[RING_SIZE];
    _Atomic uint32_t head, tail;
} ring_t;

static int ring_push(ring_t *r, const host_msg_t *m)
{
    const uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    const uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t >= RING_SIZE)
        return 0; /* full: message dropped */
    r->buf[h & (RING_SIZE - 1)] = *m;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
    return 1;
}

static int ring_pop(ring_t *r, host_msg_t *m)
{
    const uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    const uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (t == h)
        return 0;
    *m = r->buf[t & (RING_SIZE - 1)];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return 1;
}

/* One ring per producer thread. */
static ring_t g_midi_ring, g_ui_ring;
static double g_sr = 48000.0;
static _Atomic uint32_t g_load_permille_max; /* worst callback load since last display */
static mach_timebase_info_data_t g_timebase;
static volatile sig_atomic_t g_quit = 0;
static int g_verbose = 0;
static _Atomic int g_engine = SYNTH_ENGINE_VA; /* engine currently selected (for controller maps) */

/* ---------------- controller map (OID 0.2): CC -> roles or names, first match per engine ---------------- */

#define CTRL_KEYS 4
static char g_ctrl[128][CTRL_KEYS][48];
static int g_ctrl_n[128];

static int load_controller(const char *path)
{
    char err[256];
    json_t *root = json_parse_file(path, err, sizeof err);
    if (!root) {
        fprintf(stderr, "%s: %s\n", path, err);
        return -1;
    }
    const json_t *map = json_get(root, "map"), *name = json_get(root, "controller");
    int count = 0;
    for (const json_t *m = map ? map->child : NULL; m; m = m->next) {
        const int cc = atoi(m->key);
        if (cc < 0 || cc > 127)
            continue;
        const json_t *first = m->type == JSON_ARR ? m->child : m;
        for (const json_t *k = first; k && g_ctrl_n[cc] < CTRL_KEYS; k = m->type == JSON_ARR ? k->next : NULL)
            if (k->type == JSON_STR)
                snprintf(g_ctrl[cc][g_ctrl_n[cc]++], 48, "%s", k->str);
        count++;
    }
    printf("controller: %s (%d CCs mapped)\n", name && name->type == JSON_STR ? name->str : path, count);
    json_free(root);
    return 0;
}

/* CC -> parameter of the current engine (MIDI thread, not the audio thread). Returns 1 if consumed. */
static int controller_cc(int cc, int value)
{
    if (!g_ctrl_n[cc])
        return 0;
    const engine_ops_t *e = engine_registry[atomic_load(&g_engine)];
    for (int k = 0; k < g_ctrl_n[cc]; k++) {
        catalog_param_t cp;
        if (catalog_lookup(e, g_ctrl[cc][k], &cp) == 0) {
            host_msg_t m = {0};
            m.kind = MSG_PARAM;
            m.id = cp.id;
            m.value = (float)value / 127.0f;
            ring_push(&g_midi_ring, &m);
            if (g_verbose)
                printf("\rCC %d -> %s.%s = %.3g %s        \n", cc, e->name, cp.desc->name,
                       (double)param_from_norm(cp.desc, m.value), cp.desc->unit);
            return 1;
        }
    }
    return 1; /* mapped, but not for this engine: swallow it */
}

/* ---------------- audio ---------------- */

static void drain(ring_t *r)
{
    host_msg_t m;
    while (ring_pop(r, &m)) {
        switch (m.kind) {
        case MSG_MIDI: synth_midi(synth_default(), m.data, m.len, 0); break;
        case MSG_PARAM: synth_set_param(synth_default(), m.id, m.value); break;
        default: synth_set_engine(synth_default(), (synth_engine_t)m.id); break;
        }
    }
}

static OSStatus render_cb(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                          UInt32 frames, AudioBufferList *io)
{
    (void)ref;
    (void)flags;
    (void)ts;
    (void)bus;
    const uint64_t t0 = mach_absolute_time();
    drain(&g_midi_ring);
    drain(&g_ui_ring);
    float *L = io->mBuffers[0].mData, *R = io->mBuffers[1].mData;
    synth_render(synth_default(), L, R, (int)frames);
    /* load = render time / buffer duration */
    const double ns = (double)(mach_absolute_time() - t0) * g_timebase.numer / g_timebase.denom;
    const uint32_t permille = (uint32_t)(1000.0 * ns * 1e-9 * g_sr / frames);
    uint32_t prev = atomic_load_explicit(&g_load_permille_max, memory_order_relaxed);
    while (permille > prev &&
           !atomic_compare_exchange_weak_explicit(&g_load_permille_max, &prev, permille, memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
    return noErr;
}

static AudioDeviceID default_output_device(void)
{
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 size = sizeof dev;
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, &dev);
    return dev;
}

static AudioUnit start_audio(int buffer_frames)
{
    const AudioDeviceID dev = default_output_device();
    /* run at the device's own rate: no resampling */
    Float64 rate = 48000.0;
    UInt32 size = sizeof rate;
    AudioObjectPropertyAddress ar = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
    if (dev != kAudioObjectUnknown)
        AudioObjectGetPropertyData(dev, &ar, 0, NULL, &size, &rate);
    g_sr = rate;
    if (dev != kAudioObjectUnknown && buffer_frames > 0) {
        UInt32 frames = (UInt32)buffer_frames;
        AudioObjectPropertyAddress ab = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
        AudioObjectSetPropertyData(dev, &ab, 0, NULL, sizeof frames, &frames);
    }

    synth_init(synth_default(), (float)g_sr);

    AudioComponentDescription desc = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                      kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    AudioUnit unit = NULL;
    if (!comp || AudioComponentInstanceNew(comp, &unit) != noErr) {
        fprintf(stderr, "no audio output unit\n");
        return NULL;
    }
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = g_sr;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    fmt.mBytesPerPacket = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    AURenderCallbackStruct cb = {render_cb, NULL};
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt) ||
        AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) ||
        AudioUnitInitialize(unit) || AudioOutputUnitStart(unit)) {
        fprintf(stderr, "cannot start audio output\n");
        return NULL;
    }
    UInt32 bf = 0;
    size = sizeof bf;
    AudioObjectPropertyAddress ab = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
    AudioObjectGetPropertyData(dev, &ab, 0, NULL, &size, &bf);
    printf("audio: %.0f Hz, buffer %u frames (%.1f ms)\n", g_sr, (unsigned)bf, 1000.0 * bf / g_sr);
    return unit;
}

/* ---------------- MIDI ---------------- */

static MIDIClientRef g_client;
static MIDIPortRef g_port;
static _Atomic int g_midi_setup_changed = 0;
#define MAX_SOURCES 64
static MIDIUniqueID g_connected[MAX_SOURCES];
static int g_nconnected = 0;

/* UMP (MIDI 1.0 protocol): message type 2 = MIDI 1.0 channel voice in one 32-bit word. */
static void on_midi(const MIDIEventList *list, void *src)
{
    (void)src;
    const MIDIEventPacket *p = &list->packet[0];
    for (UInt32 i = 0; i < list->numPackets; i++) {
        for (UInt32 w = 0; w < p->wordCount;) {
            const uint32_t word = p->words[w];
            const unsigned mt = word >> 28;
            if (mt == 2) {
                host_msg_t m = {0};
                m.kind = MSG_MIDI;
                m.data[0] = (uint8_t)(word >> 16);
                m.data[1] = (uint8_t)((word >> 8) & 0x7F);
                m.data[2] = (uint8_t)(word & 0x7F);
                const unsigned type = m.data[0] & 0xF0;
                m.len = (type == 0xC0 || type == 0xD0) ? 2 : 3;
                if (!(type == 0xB0 && controller_cc(m.data[1], m.data[2])))
                    ring_push(&g_midi_ring, &m);
                if (g_verbose)
                    printf("\rmidi group %u: %02X %3u %3u        \n", (word >> 24) & 0xF, m.data[0], m.data[1],
                           m.data[2]);
            }
            /* UMP word count per message type */
            static const uint8_t words_for_type[16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
            w += words_for_type[mt];
        }
        p = MIDIEventPacketNext(p);
    }
}

static void connect_sources(void)
{
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; i++) {
        MIDIEndpointRef src = MIDIGetSource(i);
        MIDIUniqueID uid = 0;
        MIDIObjectGetIntegerProperty(src, kMIDIPropertyUniqueID, &uid);
        int known = 0;
        for (int k = 0; k < g_nconnected; k++)
            known |= g_connected[k] == uid;
        if (known || g_nconnected >= MAX_SOURCES)
            continue;
        if (MIDIPortConnectSource(g_port, src, NULL) == noErr) {
            g_connected[g_nconnected++] = uid;
            CFStringRef name = NULL;
            char buf[128] = "?";
            if (MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &name) == noErr && name) {
                CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
                CFRelease(name);
            }
            printf("\rmidi: connected to \"%s\"\n", buf);
        }
    }
}

static int start_midi(void)
{
    OSStatus st = MIDIClientCreateWithBlock(CFSTR("synth_play"), &g_client, ^(const MIDINotification *n) {
      if (n->messageID == kMIDIMsgSetupChanged)
          atomic_store(&g_midi_setup_changed, 1);
    });
    if (st != noErr)
        return -1;
    st = MIDIInputPortCreateWithProtocol(g_client, CFSTR("in"), kMIDIProtocol_1_0, &g_port,
                                         ^(const MIDIEventList *list, void *src) { on_midi(list, src); });
    if (st != noErr)
        return -1;
    connect_sources();
    if (g_nconnected == 0)
        printf("midi: no source yet (plug the keyboard in at any time)\n");
    return 0;
}

/* ---------------- computer keyboard ---------------- */

static struct termios g_term_saved;
static int g_term_raw = 0;

static void term_restore(void)
{
    if (g_term_raw)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_term_saved);
}

static void term_raw(void)
{
    if (!isatty(STDIN_FILENO))
        return;
    tcgetattr(STDIN_FILENO, &g_term_saved);
    struct termios t = g_term_saved;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO); /* keep ISIG: Ctrl-C still works */
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    g_term_raw = 1;
    atexit(term_restore);
}

static void on_sigint(int sig)
{
    (void)sig;
    g_quit = 1;
}

static void ui_midi(uint8_t a, uint8_t b, uint8_t c)
{
    host_msg_t m = {0};
    m.kind = MSG_MIDI;
    m.len = 3;
    m.data[0] = a;
    m.data[1] = b;
    m.data[2] = c;
    ring_push(&g_ui_ring, &m);
}

static void ui_param(int id, float norm)
{
    host_msg_t m = {0};
    m.kind = MSG_PARAM;
    m.id = id;
    m.value = norm;
    ring_push(&g_ui_ring, &m);
}

static void usage(void)
{
    printf("usage: synth_play [--engine NAME] [--controller controllers/X.json] [--buffer FRAMES] [--verbose]\n");
}

int main(int argc, char **argv)
{
    synth_engine_t engine = SYNTH_ENGINE_VA;
    int buffer = 128;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--engine") && i + 1 < argc) {
            const char *e = argv[++i];
            const engine_ops_t *ops = engine_find(e);
            if (!ops) {
                fprintf(stderr, "unknown engine: %s\n", e);
                return 2;
            }
            engine = (synth_engine_t)engine_id(ops);
        } else if (!strcmp(argv[i], "--buffer") && i + 1 < argc) {
            buffer = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--controller") && i + 1 < argc) {
            if (load_controller(argv[++i]) != 0)
                return 2;
        } else if (!strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
        } else {
            usage();
            return 2;
        }
    }

    mach_timebase_info(&g_timebase);
    AudioUnit unit = start_audio(buffer);
    if (!unit)
        return 1;
    {
        host_msg_t m = {0};
        m.kind = MSG_ENGINE;
        m.id = (int)engine;
        ring_push(&g_ui_ring, &m);
        atomic_store(&g_engine, (int)engine);
    }
    if (start_midi() != 0)
        fprintf(stderr, "midi: CoreMIDI unavailable, keyboard only\n");

    signal(SIGINT, on_sigint);
    term_raw();
    printf("keys: a w s e d f t g y h u j k o l p ; = notes | z/x octave -/+ | 1..9, 0 engine\n"
           "      c/v cutoff -/+ | b/n resonance -/+ (va)\n"
           "      . chorus  m delay  r reverb  i freeze (on/off) | space all notes off | q quit\n"
           "MIDI: CC 91 reverb, 93 chorus, 94 delay, 69 freeze, 1 mod wheel, 64 sustain\n");

    /* Computer keyboard has no key-up event: each note lasts 600 ms after its last key repeat. */
    static const char *keys = "awsedftgyhujkolp;";
    double off_time[128] = {0};
    int octave = 4;
    float cutoff = param_to_norm(&va_params[VA_P_CUTOFF], va_params[VA_P_CUTOFF].def);
    float reso = param_to_norm(&va_params[VA_P_RESO], va_params[VA_P_RESO].def);
    double t = 0.0;
    int tick = 0;

    while (!g_quit) {
        fd_set fds;
        FD_ZERO(&fds);
        struct timeval tv = {0, 10000};
        if (g_term_raw)
            FD_SET(STDIN_FILENO, &fds);
        select(g_term_raw ? STDIN_FILENO + 1 : 0, &fds, NULL, NULL, &tv);
        t += 0.01;
        char c;
        while (g_term_raw && read(STDIN_FILENO, &c, 1) == 1) {
            const char *k = strchr(keys, c);
            if (k && c) {
                const int note = 12 * (octave + 1) + (int)(k - keys);
                if (note >= 0 && note < 128) {
                    if (off_time[note] <= t)
                        ui_midi(0x90, (uint8_t)note, 100);
                    off_time[note] = t + 0.6;
                }
            } else if (c == 'z' && octave > 0) {
                octave--;
            } else if (c == 'x' && octave < 8) {
                octave++;
            } else if (c >= '0' && c <= '9' && (c == '0' ? 9 : c - '1') < SYNTH_ENGINE_COUNT) { /* 1..9, then 0 */
                host_msg_t m = {0};
                m.kind = MSG_ENGINE;
                m.id = c == '0' ? 9 : c - '1';
                ring_push(&g_ui_ring, &m);
                atomic_store(&g_engine, m.id);
                printf("\rengine %s          \n", engine_registry[m.id]->name);
            } else if (c == '.' || c == 'm' || c == 'r' || c == 'i') {
                /* effect toggles: the same messages as the MIDI CCs, through the synth's CC handling */
                static int chorus = 0, delay = 0, reverb = 0, freeze = 0;
                int *flag = c == '.' ? &chorus : c == 'm' ? &delay : c == 'r' ? &reverb : &freeze;
                *flag = !*flag;
                const uint8_t cc = c == '.' ? 93 : c == 'm' ? 94 : c == 'r' ? 91 : 69;
                const uint8_t val = c == 'i' ? (*flag ? 127 : 0) : (*flag ? (c == 'r' ? 60 : 45) : 0);
                ui_midi(0xB0, cc, val);
                printf("\r%s %s          \n", c == '.' ? "chorus" : c == 'm' ? "delay" : c == 'r' ? "reverb" : "freeze",
                       *flag ? "on" : "off");
            } else if (c == 'c' || c == 'v') {
                cutoff = fminf(1.0f, fmaxf(0.0f, cutoff + (c == 'v' ? 0.03f : -0.03f)));
                ui_param(VA_P_CUTOFF, cutoff);
                printf("\rcutoff %.0f Hz          \n", (double)param_from_norm(&va_params[VA_P_CUTOFF], cutoff));
            } else if (c == 'b' || c == 'n') {
                reso = fminf(1.0f, fmaxf(0.0f, reso + (c == 'n' ? 0.05f : -0.05f)));
                ui_param(VA_P_RESO, reso);
                printf("\rresonance Q %.2f          \n", (double)param_from_norm(&va_params[VA_P_RESO], reso));
            } else if (c == ' ') {
                ui_midi(0xB0, 123, 0);
            } else if (c == 'q') {
                g_quit = 1;
            }
        }
        for (int n = 0; n < 128; n++) {
            if (off_time[n] > 0.0 && off_time[n] <= t) {
                ui_midi(0x80, (uint8_t)n, 0);
                off_time[n] = 0.0;
            }
        }
        if (atomic_exchange(&g_midi_setup_changed, 0))
            connect_sources();
        if (++tick % 100 == 0) {
            const uint32_t l = atomic_exchange(&g_load_permille_max, 0);
            printf("\rmax callback load: %5.1f %%  octave %d ", l / 10.0, octave);
            fflush(stdout);
        }
    }
    printf("\n");
    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    return 0;
}
