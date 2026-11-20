# Board link: driving a board over Ethernet

A host (the Mac today, the Pi hub tomorrow) drives each synth board with UDP datagrams on its Ethernet
port. It can play notes and controllers, set parameters, send a patch or pick a built-in preset, and upload a
sample kit. Code:

| Piece | Where |
|---|---|
| Encoding and decoding, transfers (portable C, no allocation) | `midi/link.h`, `midi/link.c` |
| The sample kit format, read by the sampler engine | `dsp/core/kit.h` |
| The board's IPv4 stack: Ethernet, ARP, ping, UDP | `midi/udpip.h`, `midi/udpip.c` |
| The queue from the network core to the audio core | `midi/linkq.h` |
| H745: Ethernet (MII, LAN8742), the interrupt, presets and kits | `target/h745/app/m4/net.c` |
| H745: events applied at the start of each audio buffer | `target/h745/app/app.c` (`net_events`) |
| Host tool, and a fake board for tests | `host/synth_link.c` |
| Tests | `tests/test_link.c` |

## Network

- **Addresses.** Every board picks a link-local IPv4 address (169.254.x.y, RFC 3927) and a locally
  administered MAC (02:4F:…). Both are derived from the MCU's unique id, so they survive resets.
  Nothing needs configuring: a Mac or a Pi on a direct cable, or on a switch without DHCP, gives itself a
  169.254 address too.
- **Reaching a board.** Boards listen on UDP port **47100**, on their own address, on broadcast, and on the
  multicast group **239.255.71.1** (for the hub). The header names the destination board, or every board.
  The host can therefore send to a broadcast address whatever its own subnet is.
- **Replies.** A board always answers the MAC, IP and port the request came from. It keeps no ARP table.
- **Datagrams.** One datagram is at most 1 472 bytes, so it is never fragmented; the board drops IP
  fragments. The board also answers ping.

## Datagrams

Every datagram starts with a 12-byte header. All fields are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `'O' 'L'` |
| 2 | 1 | version (1) |
| 3 | 1 | type |
| 4 | 4 | destination board id (0 = every board); the sender's id in replies |
| 8 | 4 | sequence number, chosen by the sender and echoed by `ACK` |

### Messages

| Type | Name | Direction | Payload | Reply |
|---|---|---|---|---|
| 0x01 | DISCOVER | host → boards | nothing | `HELLO` |
| 0x02 | HELLO | board → host | id, unique id, IP, catalog hash, preset playing, preset count, DSP load average and peak, overruns, kit size, board name, name of the preset playing | — |
| 0x03 | ACK | board → host | `seq u32`, `status u8` | — |
| 0x04 | HEARTBEAT | host → boards | nothing: "still here" | none |
| 0x10 | MIDI | host → boards | MIDI 1.0 messages back to back, each with its status byte (channel messages and real-time only) | none |
| 0x11 | PARAM | host → boards | `catalog u32`, `count u8`, then `count × {id u16, value f32}` (normalized 0..1) | none |
| 0x12 | SELECT | host → boards | `name[32]`: a built-in preset | `ACK` |
| 0x13 | PRESET | host → boards | `catalog u32`, `engine u8`, `count u8`, `name[32]`, then `count × {id u16, value f32}` | `ACK` |
| 0x20 | XFER_BEGIN | host → board | `xfer u32`, `kind u8`, `size u32`, `crc u32`, `name[32]` | `XFER_ACK` |
| 0x21 | XFER_DATA | host → board | `xfer u32`, `offset u32`, then up to 1 024 bytes | `XFER_ACK` |
| 0x22 | XFER_ACK | board → host | `xfer u32`, `next u32` (bytes received in order), `status u8` | — |

**Status codes:**

| Code | Name | Meaning |
|---|---|---|
| 0 | ok | |
| 1 | busy | The previous preset is not applied yet, or (to `XFER_BEGIN`) the board still plays the kit in the bank the new one would use; send again. |
| 2 | malformed | |
| 3 | catalog | The parameters were made for another build. |
| 4 | unknown | No built-in preset has this name. |
| 5 | too big | |
| 6 | corrupted | The CRC does not match. |
| 7 | pending | |

### Fail-safe: heartbeat and silence

A note played over the network ends with a note-off that may never come: cable pulled, hub crashed, a datagram
lost, a full event queue. So a board that has received MIDI stops every note (all notes off and sustain off on
the 16 channels, release tails play) when:
- nothing has come for **500 ms** (`LINK_SILENCE_MS`): a host that plays sends MIDI or a `HEARTBEAT` at least
  every **100 ms** (`LINK_HEARTBEAT_MS`), also while notes are only held;
- the cable goes down;
- the event queue overflowed (some events were lost).

Then it waits for MIDI again. A host that only sends presets or kits needs no heartbeat.

### Catalog hash

A parameter is identified by an id and a 0..1 value. Its meaning depends on the build: which engines exist,
in which order, and the name, range and curve of each parameter. `catalog_hash()` (in `host/catalog.h`)
reduces all of this to 32 bits:

- the board carries the hash it was built with (`presets_gen.c`, `preset_catalog`);
- `PARAM` and `PRESET` carry the sender's hash;
- if the two hashes differ, the board refuses the message instead of playing the wrong parameters.

`synth_link discover` warns when the hashes differ. The fix is to rebuild and flash the board.

### Transfers

A transfer is the bytes of a large object; today only `kind 1`, a sample kit, exists.

**Board side.**
- It keeps the bytes in order only. A chunk at any other offset (lost, repeated, or early) is ignored.
- It acknowledges every chunk with `next`, the number of bytes it holds in order.
- When the last byte arrives, it checks the CRC-32 of the whole transfer (IEEE 802.3, the same as zlib).
  The answer is `ok` or `corrupted`.

**Host side.**
- It sends 16 chunks ahead of the last acknowledgement.
- It goes back to `next` in two cases: three acknowledgements in a row at the same position (a chunk was
  lost), or 30 ms without any acknowledgement.

**On the board** the CRC takes one table lookup per byte (about 40 µs per 1 KB chunk on the M4, estimated) and
the bytes go to the SDRAM as 32-bit writes. The receive ring has 4 descriptors (`ETH_RX_DESC_CNT` in CubeMX's
`stm32h7xx_hal_conf.h`): raising it to 8 or more in CubeMX's Ethernet settings gives MIDI more room during an
upload.

**Measured on this Mac against the fake board:** 1 MB takes 0.03 s with no loss, and 0.36 s when 10 % of the
datagrams are lost. On the board, the CRC is computed bit by bit on the M4, so expect a few MB/s.

### Sample kits

The layout of a kit:

1. **Header (8 bytes):** `"OIDK"`, `version u16` (1), `count u16` (at most 128).
2. **Directory:** `count` entries of 32 bytes, each `{name[20], note u8, flags u8, reserved u16, offset u32, frames u32}`.
3. **Samples:** 16-bit signed mono PCM at 48 kHz. Each sample starts at its `offset`, which is even and
   counted from the start of the kit.

There is one sample per MIDI note.

**Upload.** `synth_link kit 36:kick.wav 38:snare.wav` builds the kit:
- it reads WAV files in 16-bit or 24-bit PCM, or 32-bit float;
- it mixes the channels down to mono;
- it resamples to 48 kHz with the sampler's band-limited player (`host/kitfile.c`).

**Storage on the H745.**
- The board writes the kit to the SDRAM, into one of two 7.5 MB banks, alternately, so a kit can keep playing
  while the next one arrives.
- It checks the CRC, then the directory. Only then does it publish the kit to the M7 through the mailbox
  (`kit_seq`, `kit_addr`, `kit_size`, `kit_count`).
- The M7 reads the SDRAM through its data cache (MPU region 2, write-through). It invalidates the kit's lines,
  checks the directory again, and gives the kit to the `sampler` engine at the start of the next audio buffer
  (or at once while it is silent). It keeps it across preset changes, then confirms with `kit_ack_seq`.
- A new transfer is answered `busy` until that confirmation, so a bank is never overwritten while it is read.

## On the H745

The M4 owns Ethernet. Received frames are handled inside its Ethernet interrupt, which has priority 1, above
the touch panel and the timers. Three things follow:

- **MIDI and PARAM** go into a lock-free queue in SRAM4 (`LINKQ`, 256 events). The M7 drains it at the start
  of every audio buffer, so an event waits at most one buffer: 2.7 ms at 128 frames. Events that arrive
  during a preset change are dropped. The first note-on from the network stops the demo arpeggio.
- **Silence** (above): the M4 counts the datagrams (`net_alive`), the MIDI ones (`net_midi`) and the cable
  state (`net_up`) in the mailbox; the M7's main loop decides, and its audio interrupt stops the notes.
- **SELECT and PRESET** go through the existing mailbox request, with the same fade as a tap on the screen. A
  received preset is copied to `NET_PRESET` in SRAM4. The board answers `busy` until the M7 has applied the
  previous request (`done_seq`), so `NET_PRESET` is never rewritten while it is read.
  Wavetable patches are refused on the board.
- **Sending** never waits. The Ethernet interrupt outranks the tick, so a timeout could not run out. A reply
  is copied into one of four transmit buffers. If none is free, the reply is dropped and counted.

The DMA cannot address the M4's 0x10000000 view of the D2 SRAM. `net.c` hands it the 0x30000000 address of
the same memory instead.

The top bar of the instrument screen shows the cable state (LAN), and the kit once one has been received.

## Use

```sh
just build
./build-host/host/synth_link --iface en5 discover   # the interface the board is cabled to (ifconfig)
./build-host/host/synth_link --iface en5 note 60 100 500
./build-host/host/synth_link --iface en5 preset mono_d_lead
./build-host/host/synth_link serve                  # a fake board on this machine (SYNTH_LINK_LOSS=5: 5 % loss)
./build-host/host/synth_link --to 127.0.0.1 discover
```

`--iface` matters on a Mac with Wi-Fi: broadcasts otherwise leave by the default route. `--board ID`
addresses a single board.

**Python client** (`tools/board.py`, also importable: `from board import Link`): discover, select, notes,
phrases in the patches' audition notation, knob sweeps, and a test of every preset over the cable. The host is
the sequencer: notes are sent when they are due, with the heartbeat while they are held.

```sh
tools/board.py --iface en0 discover
tools/board.py --iface en0 audition gull_laugh
tools/board.py --iface en0 play "A2 A2 C3 A2 - A2 G2 A2" --bpm 130 --loop 4 --sweep 13:10:120
tools/board.py --iface en0 test --seconds 2      # every preset: select, play its phrase, read the load
```

With `--iface`, it sends to that interface's subnet broadcast. A board accepts any IPv4 broadcast that came as
an Ethernet broadcast, so a host with a fixed address (192.168.100.1 on a direct cable) reaches a board on its
link-local address without configuring anything; the board answers the sender's MAC and IP directly.

## Not done yet

- Tried on the board (2026-11-20, direct cable to a Mac): discovery, select, MIDI, heartbeat, all 81 presets played
  over the network without an overrun. Kits over the network and the timings are still to measure.
- Numbered MIDI datagrams, so a board can tell a lost one (protocol v2, with the timestamps).
- Timestamps: a note is played at the start of the next buffer. Sample-accurate timing will need a
  synchronized clock between the hub and the boards.
- DHCP, if the Pi serves addresses.
- Persistent presets in the board's flash memory.
- A CoreMIDI bridge, so a keyboard on the Mac plays the board.
