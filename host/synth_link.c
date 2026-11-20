/* synth_link.c — drives synth boards over Ethernet with the board link (midi/link.h, docs/link.md): finds
 * them, plays notes and controllers, sends a patch or the name of a built-in preset, uploads a sample kit
 * made from WAV files. "serve" plays the board's part on this machine (prints what it receives, takes
 * transfers), to try a host or the hub without a board.
 *
 *   synth_link [--iface IF] [--to ADDR] [--board ID] COMMAND ...
 *     discover                      list the boards that answer
 *     note NOTE [VEL [MS]]          note-on, then note-off after MS (default 300), channel 1
 *     cc NUM VALUE                  control change, channel 1
 *     midi HEX...                   raw MIDI bytes ("90 3c 64")
 *     select NAME                   play the board's built-in preset NAME
 *     preset NAME|FILE.json         send a patch (patches/NAME.json) and play it
 *     kit DIR | NOTE:FILE.wav ...   upload a sample kit: a directory of NN_name.wav files, or files with
 *                                   their notes (converted to 16-bit mono 48 kHz, host/kitfile.h)
 *     serve                         act as a board on port 47100 (prints what arrives); SYNTH_LINK_LOSS=5
 *                                   drops 5 % of the datagrams it receives
 */
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "core/kit.h"
#include "kitfile.h"
#include "link.h"
#include "patch.h"

static int sock = -1;
static struct sockaddr_in dest;
static uint32_t board = LINK_ALL, seq_no = 1;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void die(const char *msg)
{
    fprintf(stderr, "synth_link: %s\n", msg);
    exit(1);
}

static void open_socket(const char *iface, int port)
{
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        die("cannot open a UDP socket");
    const int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (iface) {
        const unsigned idx = if_nametoindex(iface);
        if (!idx)
            die("unknown network interface");
#ifdef IP_BOUND_IF
        setsockopt(sock, IPPROTO_IP, IP_BOUND_IF, &idx, sizeof idx); /* broadcasts leave by this interface */
#endif
        struct in_addr a;
        a.s_addr = htonl(INADDR_ANY);
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &a, sizeof a);
    }
    struct sockaddr_in me = {0};
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port = htons((uint16_t)port);
    if (bind(sock, (struct sockaddr *)&me, sizeof me) < 0)
        die("cannot bind the UDP port");
}

static void send_datagram(const uint8_t *d, int n)
{
    if (n < 0)
        die("message too large");
    if (sendto(sock, d, (size_t)n, 0, (struct sockaddr *)&dest, sizeof dest) != n)
        die(strerror(errno));
}

/* Waits up to ms for a datagram; returns its length (header checked), -1 on timeout. */
static int receive(uint8_t *buf, link_header_t *h, double ms, struct sockaddr_in *from)
{
    const double end = now_ms() + ms;
    for (;;) {
        const double left = end - now_ms();
        if (left <= 0)
            return -1;
        struct timeval tv = {(time_t)(left / 1e3), (int)((long)(left * 1e3) % 1000000)};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        socklen_t fl = sizeof *from;
        const ssize_t n = recvfrom(sock, buf, LINK_MAX_DATAGRAM, 0, (struct sockaddr *)from, &fl);
        if (n < 0)
            continue;
        const int len = link_get_header(buf, (int)n, h);
        if (len >= 0)
            return len;
    }
}

static link_header_t header(void) { return (link_header_t){0, board, seq_no++}; }

/* ---------------- commands ---------------- */

static const char *preset_name(const link_hello_t *m)
{
    return m->preset == -1 ? "(none)" : m->playing;
}

static int cmd_discover(void)
{
    uint8_t buf[LINK_MAX_DATAGRAM];
    link_header_t h = header();
    h.type = LINK_DISCOVER;
    link_put_header(buf, &h);
    send_datagram(buf, LINK_HEADER);
    int found = 0;
    link_header_t r;
    struct sockaddr_in from;
    int len;
    while ((len = receive(buf, &r, 700, &from)) >= 0) {
        link_hello_t m;
        if (r.type != LINK_HELLO || link_decode_hello(buf + LINK_HEADER, len, &m) < 0)
            continue;
        char ip[32];
        snprintf(ip, sizeof ip, "%u.%u.%u.%u", m.ip >> 24, (m.ip >> 16) & 255, (m.ip >> 8) & 255, m.ip & 255);
        printf("%08x  %-6s %-15s  %-24s load %u%% (peak %u%%)  overruns %u  kit %u bytes  %d presets%s\n",
               (unsigned)m.id, m.board, ip, preset_name(&m), m.load_pct, m.peak_pct, (unsigned)m.overruns,
               (unsigned)m.kit_size, m.preset_count,
               m.catalog == catalog_hash() ? "" : "  [other build: parameters refused, rebuild and flash]");
        found++;
    }
    if (!found)
        fprintf(stderr, "synth_link: no board answered (cable, interface: --iface)\n");
    return found ? 0 : 1;
}

static void send_midi(const uint8_t *bytes, int n)
{
    uint8_t buf[LINK_MAX_DATAGRAM];
    link_header_t h = header();
    send_datagram(buf, link_encode_midi(buf, &h, bytes, n));
}

/* Sends a SELECT or PRESET until the board acknowledges it (it answers BUSY while a preset fades). */
static int send_acked(const uint8_t *d, int n, uint32_t seq)
{
    uint8_t buf[LINK_MAX_DATAGRAM];
    for (int attempt = 0; attempt < 20; attempt++) {
        send_datagram(d, n);
        link_header_t r;
        struct sockaddr_in from;
        int len;
        const double end = now_ms() + 250;
        while ((len = receive(buf, &r, end - now_ms(), &from)) >= 0) {
            uint32_t s;
            uint8_t st;
            if (r.type != LINK_ACK || link_decode_ack(buf + LINK_HEADER, len, &s, &st) < 0 || s != seq)
                continue;
            static const char *what[] = {"ok", "busy", "malformed", "parameters of another build (rebuild and flash)",
                                         "unknown preset", "too big", "corrupted", "pending"};
            if (st == LINK_BUSY)
                break; /* again, after a short wait */
            if (st != LINK_OK) {
                fprintf(stderr, "synth_link: board %08x: %s\n", (unsigned)r.dest, st < 8 ? what[st] : "error");
                return 1;
            }
            printf("board %08x: ok\n", (unsigned)r.dest);
            return 0;
        }
        usleep(50000);
    }
    fprintf(stderr, "synth_link: no acknowledgement\n");
    return 1;
}

static int cmd_select(const char *name)
{
    uint8_t buf[LINK_MAX_DATAGRAM];
    link_header_t h = header();
    return send_acked(buf, link_encode_select(buf, &h, name), h.seq);
}

static int cmd_preset(const char *arg)
{
    char path[1024], err[512], warn[512];
    if (strchr(arg, '/') || strstr(arg, ".json"))
        snprintf(path, sizeof path, "%s", arg);
    else
        snprintf(path, sizeof path, "patches/%s.json", arg);
    static patch_t p;
    if (patch_load(path, &p, err, sizeof err, warn, sizeof warn) != 0)
        die(err);
    static link_preset_t m;
    m.catalog = catalog_hash();
    m.engine = 0xFF;
    for (int i = 0; i < SYNTH_ENGINE_COUNT; i++)
        if (engine_registry[i] == p.engine)
            m.engine = (uint8_t)i;
    if (p.n > LINK_PARAMS_MAX)
        die("patch with too many parameters for one message");
    m.count = (uint8_t)p.n;
    snprintf(m.name, sizeof m.name, "%s", p.name);
    for (int k = 0; k < p.n; k++)
        m.params[k] = (link_param_t){(uint16_t)p.ids[k], param_to_norm(p.desc[k], p.values[k])};
    uint8_t buf[LINK_MAX_DATAGRAM];
    link_header_t h = header();
    return send_acked(buf, link_encode_preset(buf, &h, &m), h.seq);
}

static int cmd_kit(int argc, char **argv)
{
    /* a directory of NN_name.wav files, or NOTE:FILE.wav arguments (host/kitfile.h) */
    size_t size = 0;
    char err[1024];
    struct stat st;
    uint8_t *kit = argc == 1 && stat(argv[0], &st) == 0 && S_ISDIR(st.st_mode)
                       ? kitfile_load_dir(argv[0], &size, err, sizeof err)
                       : kitfile_load_args(argv, argc, &size, err, sizeof err);
    if (!kit) {
        fprintf(stderr, "synth_link: kit: %s\n", err);
        return 1;
    }
    argc = kit_count(kit, size);

    /* transfer: BEGIN until acknowledged, then a window of chunks; the board acknowledges each chunk with the
     * bytes it has in order, and we resend from there when the acknowledgements stop moving */
    link_xfer_t x = {(uint32_t)time(NULL) ^ (uint32_t)getpid(), LINK_KIND_KIT, size, link_crc32(0, kit, size), "kit"};
    uint8_t buf[LINK_MAX_DATAGRAM], in[LINK_MAX_DATAGRAM];
    const int window = 16;
    uint32_t base = 0, sent = 0;
    int began = 0, idle = 0, dups = 0, resent = 0, busy_count = 0;
    const double t0 = now_ms();
    for (;;) {
        link_header_t h = header();
        if (!began)
            send_datagram(buf, link_encode_xfer_begin(buf, &h, &x));
        while (began && sent < size && sent < base + (uint32_t)window * LINK_CHUNK) {
            const int n = size - sent < LINK_CHUNK ? (int)(size - sent) : LINK_CHUNK;
            h = header();
            send_datagram(buf, link_encode_xfer_data(buf, &h, x.xfer, sent, kit + sent, n));
            sent += (uint32_t)n;
        }
        /* one acknowledgement (a LAN answers within a millisecond: 30 ms of silence means a loss) */
        link_header_t r;
        struct sockaddr_in from;
        uint32_t xf = 0, next = 0;
        uint8_t st = 0;
        int len;
        do
            len = receive(in, &r, began ? 30 : 300, &from);
        while (len >= 0 && (r.type != LINK_XFER_ACK ||
                            link_decode_xfer_ack(in + LINK_HEADER, len, &xf, &next, &st) < 0 || xf != x.xfer));
        if (len < 0) {
            if (++idle > (began ? 100 : 10))
                die(began ? "kit: the board stopped answering" : "kit: no board answered");
            sent = base; /* go back to what the board has */
            resent = 1;
            continue;
        }
        idle = 0;
        if (st == LINK_OK) {
            const double s = (now_ms() - t0) / 1e3;
            printf("kit: %d samples, %u bytes in %.2f s (%.0f kB/s), checked by the board\n", argc, (unsigned)size,
                   s, size / 1e3 / (s > 0 ? s : 1));
            free(kit);
            return 0;
        }
        if (st == LINK_BUSY && !began) { /* the board still plays the kit in the bank this one would overwrite */
            if (++busy_count > 100)
                die("kit: the board stays busy");
            usleep(20000);
            continue;
        }
        if (st != LINK_PENDING) {
            fprintf(stderr, "synth_link: kit refused: %s\n",
                    st == LINK_CRC ? "corrupted" : st == LINK_TOO_BIG ? "too big for the board" : "malformed");
            free(kit);
            return 1;
        }
        began = 1;
        if (next > base) {
            base = next;
            dups = resent = 0;
            if (sent < base)
                sent = base;
        } else if (!resent && sent > base && ++dups >= 3) { /* the same position three times: a chunk is lost */
            sent = base;
            dups = 0;
            resent = 1; /* the rest of the old window will repeat it: ignore until it moves */
        }
    }
}

/* ---------------- serve: a board on this machine ---------------- */

static int cmd_serve(void)
{
    static uint8_t mem[16u << 20];
    link_rx_t rx;
    link_rx_init(&rx, mem, sizeof mem);
    uint8_t buf[LINK_MAX_DATAGRAM], out[LINK_MAX_DATAGRAM];
    const uint32_t me = 0x5E57EDu;
    const int loss = getenv("SYNTH_LINK_LOSS") ? (int)(atof(getenv("SYNTH_LINK_LOSS")) * 10) : 0; /* percent */
    printf("serving as board %08x on UDP port %u (Ctrl-C to stop)\n", (unsigned)me, LINK_PORT);
    for (;;) {
        link_header_t h;
        struct sockaddr_in from;
        const int len = receive(buf, &h, 1e9, &from);
        if (len < 0 || !link_for_me(h.dest, me))
            continue;
        if (loss > 0 && rand() % 1000 < loss)
            continue; /* SYNTH_LINK_LOSS: a lossy network, to test the senders */
        const uint8_t *p = buf + LINK_HEADER;
        link_header_t rh = {0, me, h.seq};
        int n = -1;
        switch (h.type) {
        case LINK_DISCOVER: {
            link_hello_t m = {0};
            m.id = me;
            m.ip = 0x7F000001u;
            m.catalog = catalog_hash();
            m.preset = -1;
            m.kit_size = rx.status == LINK_OK ? rx.info.size : 0;
            snprintf(m.board, sizeof m.board, "host");
            n = link_encode_hello(out, &rh, &m);
            break;
        }
        case LINK_MIDI: {
            int pos = 0, k, start = 0;
            printf("midi:");
            while ((k = link_midi_next(p, len, &pos)) > 0) {
                for (int i = start; i < pos; i++)
                    printf(" %02x", p[i]);
                printf(" |");
                start = pos;
            }
            printf(k < 0 ? " (malformed)\n" : "\n");
            break;
        }
        case LINK_SELECT: {
            char name[LINK_NAME];
            if (link_decode_select(p, len, name) == 0)
                printf("select %s\n", name);
            n = link_encode_ack(out, &rh, h.seq, LINK_OK);
            break;
        }
        case LINK_PRESET: {
            static link_preset_t m;
            const int bad = link_decode_preset(p, len, &m) < 0;
            const uint8_t st = bad ? LINK_BAD : m.catalog != catalog_hash() ? LINK_CATALOG : LINK_OK;
            if (!bad)
                printf("preset %s: engine %d, %d parameters\n", m.name, m.engine, m.count);
            n = link_encode_ack(out, &rh, h.seq, st);
            break;
        }
        case LINK_XFER_BEGIN: {
            link_xfer_t x;
            if (link_decode_xfer_begin(p, len, &x) < 0)
                break;
            const uint8_t st = link_rx_begin(&rx, &x);
            printf("transfer %08x: %u bytes\n", (unsigned)x.xfer, (unsigned)x.size);
            n = link_encode_xfer_ack(out, &rh, x.xfer, rx.next, st);
            break;
        }
        case LINK_XFER_DATA: {
            uint32_t xf, off;
            const uint8_t *d;
            int dn;
            if (link_decode_xfer_data(p, len, &xf, &off, &d, &dn) < 0)
                break;
            const uint32_t before = rx.next;
            const uint8_t st = link_rx_data(&rx, xf, off, d, dn);
            if (st == LINK_OK && before != rx.next)
                printf("transfer %08x complete: %d samples\n", (unsigned)xf, kit_count(mem, rx.info.size));
            else if (st == LINK_CRC && before != rx.next)
                printf("transfer %08x corrupted\n", (unsigned)xf);
            n = link_encode_xfer_ack(out, &rh, xf, rx.next, st);
            break;
        }
        default:
            break;
        }
        fflush(stdout);
        if (n > 0)
            sendto(sock, out, (size_t)n, 0, (struct sockaddr *)&from, sizeof from);
    }
}

/* ---------------- main ---------------- */

static void usage(void)
{
    fprintf(stderr, "usage: synth_link [--iface IF] [--to ADDR] [--board ID] discover | note NOTE [VEL [MS]] |\n"
                    "       cc NUM VALUE | midi HEX... | select NAME | preset NAME|FILE.json |\n"
                    "       kit DIR|NOTE:FILE.wav... | serve\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *iface = NULL, *to = "255.255.255.255";
    int i = 1;
    for (; i + 1 < argc && !strncmp(argv[i], "--", 2); i += 2) {
        if (!strcmp(argv[i], "--iface"))
            iface = argv[i + 1];
        else if (!strcmp(argv[i], "--to"))
            to = argv[i + 1];
        else if (!strcmp(argv[i], "--board"))
            board = (uint32_t)strtoul(argv[i + 1], NULL, 16);
        else
            usage();
    }
    if (i >= argc)
        usage();
    const char *cmd = argv[i++];
    const int rest = argc - i;
    char **arg = argv + i;

    open_socket(iface, !strcmp(cmd, "serve") ? LINK_PORT : 0);
    dest.sin_family = AF_INET;
    dest.sin_port = htons(LINK_PORT);
    if (inet_pton(AF_INET, to, &dest.sin_addr) != 1)
        die("--to: not an IPv4 address");

    if (!strcmp(cmd, "discover"))
        return cmd_discover();
    if (!strcmp(cmd, "serve"))
        return cmd_serve();
    if (!strcmp(cmd, "note") && rest >= 1) {
        const int note = atoi(arg[0]), vel = rest > 1 ? atoi(arg[1]) : 100, ms = rest > 2 ? atoi(arg[2]) : 300;
        const uint8_t on[3] = {0x90, (uint8_t)(note & 127), (uint8_t)(vel & 127)}, off[3] = {0x80, on[1], 0};
        send_midi(on, 3);
        /* held: a heartbeat keeps the board from stopping the note (LINK_SILENCE_MS) */
        uint8_t hb[LINK_HEADER];
        for (int t = 0; t < ms; t += LINK_HEARTBEAT_MS) {
            usleep((useconds_t)(ms - t < LINK_HEARTBEAT_MS ? ms - t : LINK_HEARTBEAT_MS) * 1000);
            link_header_t h = header();
            h.type = LINK_HEARTBEAT;
            link_put_header(hb, &h);
            send_datagram(hb, LINK_HEADER);
        }
        send_midi(off, 3);
        return 0;
    }
    if (!strcmp(cmd, "cc") && rest == 2) {
        const uint8_t m[3] = {0xB0, (uint8_t)(atoi(arg[0]) & 127), (uint8_t)(atoi(arg[1]) & 127)};
        send_midi(m, 3);
        return 0;
    }
    if (!strcmp(cmd, "midi") && rest >= 1) {
        uint8_t m[256];
        int n = 0;
        for (int k = 0; k < rest && n < 256; k++)
            m[n++] = (uint8_t)strtoul(arg[k], NULL, 16);
        int pos = 0, r;
        while ((r = link_midi_next(m, n, &pos)) > 0)
            ;
        if (r < 0)
            die("midi: complete channel or real-time messages only, each with its status byte");
        send_midi(m, n);
        return 0;
    }
    if (!strcmp(cmd, "select") && rest == 1)
        return cmd_select(arg[0]);
    if (!strcmp(cmd, "preset") && rest == 1)
        return cmd_preset(arg[0]);
    if (!strcmp(cmd, "kit"))
        return cmd_kit(rest, arg);
    usage();
    return 2;
}
