# Heartbeat Soundtracker Player Manual

**C/Oscar64 port of the Heartbeat Soundtracker standalone player, for the Ultimate 64 and C64 Ultimate**

Module files (ultimate-uci-oscar64 1.4.0):
- `include/ultimate_hbplay_lib.h` / `include/ultimate_hbplay_lib.c`
- `include/heartbeat/*.bin` -- the original player's tempo and frequency
  tables, embedded at compile time

Uses `ultimate_audio_lib` (Ultimate Audio DMA and REU fetch, see
[`ULTIMATEAUDIO_MANUAL.md`](ULTIMATEAUDIO_MANUAL.md)) and `ultimate_common_lib` /
`ultimate_dos_lib` (loading the song into the REU, see
[`UCILIB_MANUAL.md`](UCILIB_MANUAL.md)); `#include "ultimate_hbplay_lib.h"`
compiles them in.

Ported from the Heartbeat Soundtracker standalone player (6502 assembly) by
Aleksi Eeben / Eight Bit Shed, which is distributed to licence holders. The C
port was written by Xander Mol for
[heartbeat-demo](https://github.com/xahmol/heartbeat-demo) and moved into this
library in 1.4.0. The port and the tables are published with Aleksi Eeben's
written permission -- see [`NOTICE.md`](../NOTICE.md). The original assembly
source is not part of this repository.

This manual covers the public API, the song file format, the track commands
and (section 9) the internal design: interrupt and tick, zero page, register
flushing, frequency conversion and fixed-point formats.

---

## Contents

1. [Overview](#1-overview)
2. [Quick Start](#2-quick-start)
3. [Public API Reference](#3-public-api-reference)
4. [Song Data Structures](#4-song-data-structures)
5. [Player-Internal State](#5-player-internal-state)
6. [Track Command Reference](#6-track-command-reference)
7. [Firmware Prerequisites](#7-firmware-prerequisites)
8. [Fidelity Notes and Known Limitations](#8-fidelity-notes-and-known-limitations)
9. [Internals](#9-internals)

---

## 1. Overview

Heartbeat Soundtracker is a music tracker built specifically around the Ultimate 64's
combination of SID chip(s) + Ultimate Audio DMA sample channels + 16 MB REU. A song
(`.reu` file) contains sequencer/pattern data, up to 8 SID chips' worth of 3-channel
synth tracks, and 7 Ultimate Audio DMA sample tracks, all driven by one tick IRQ.

This library plays such a song file on real Ultimate 64 hardware:

- Loads a `.reu` file into the REU via the UCI, from the current or the home
  directory.
- Streams pattern data from REU one row at a time as playback advances (not
  preloaded into C64 RAM — patterns can be arbitrarily large).
- Drives up to 8 SID chips (3 channels each) and all 7 Ultimate Audio DMA channels
  from a single CIA1 Timer A interrupt, tempo-synced via the song's own BPM.
- Implements the full per-tick modulation set: vibrato, pulse-width sweep, SID
  filter cutoff sweep, wave/arpeggio table stepping, and portamento (both SID and
  Ultimate Audio).
- Implements all 11 in-pattern track commands (`Pa`/`Bt`/`Dn`/`Fi`/`Iv`/`Le`/`Co`/
  `Po`/`Up`/`Vo`/`Xt`).
- Supports manual one-off sample triggering (`uii_hbplay_play_fx`/`uii_hbplay_stop_fx`) independent
  of song playback, e.g. for a demo's own sound effects.
- Detects PAL/NTSC at startup and adjusts SID frequency tables and tempo timing
  accordingly.

The goal of this port is **bit-exact behavioral parity** with the original assembly
player, not a reinterpretation — every routine is a direct, traceable translation,
documented with its own comment pointing at the corresponding label in `player.s`.

---

## 2. Quick Start

```c
#include "ultimate_hbplay_lib.h"
#include "ultimate_turbo_lib.h"

// Turbo MUST be engaged before starting playback -- uii_hbplay_tick's per-tick work
// (full Modulations pass over up to 24 SID channels + 7 UA channels, every
// tick) does not reliably fit the ~5000 CPU cycles the CIA1 Timer A period
// gives at 1 MHz. See section 9.
uii_turbo_fast();

if (uii_hbplay_load("My Song.reu", 0x000000UL))
{
    uii_hbplay_detect_ntsc();      // must run before uii_hbplay_init()
    uii_hbplay_init(0, 1);         // seq_start_pos=0, play_mode=1 (play song)

    // Playback now runs entirely in the background via the tick IRQ.
    // Your own code is free to run here -- poll uii_hbplay_ext_out for Xt-command
    // sync cues, trigger one-off effects with uii_hbplay_play_fx(), etc.

    for (;;)
    {
        // ... your demo code ...
    }
}
```

To stop playback and silence everything:

```c
uii_hbplay_stop_all();
```

---

## 3. Public API Reference

### `char uii_hbplay_load(char *filename, unsigned long reu_addr)`

Opens `filename` in the current UCI directory, or else in the UCI home
directory, loads the whole file into the REU starting at `reu_addr` in
32767-byte chunks (`uii_load_reu_at`), closes it, then reads the `$2000`-byte
song-data header (fixed REU offset `reu_addr + $00E000`, matching the file's own
internal layout) into `uii_hbplay_songdata`. Sets `uii_hbplay_state.reu_song_base`.

Returns 1 on success, 0 if the file could not be found or loaded.

To load from another place, change directory first (`uii_change_dir()`), or
search the drives with `uii_scan_media()`/`uii_find_media_path()`, as
heartbeat-demo does for its install folder. The file name goes to the UCI
byte for byte, as ASCII. A program that includes `petscii.h` has a character
map that swaps upper- and lower-case letters in string literals; put
`#pragma charmap(97, 97, 26)` and `#pragma charmap(65, 65, 26)` before such a
file name and restore the map after it, as heartbeat-demo does.

### `char uii_hbplay_detect_ntsc(void)`

Raster-line PAL/NTSC detection. Sets `uii_hbplay_state.ntsc_detected` (1 = NTSC). **Must be
called before `uii_hbplay_init()`** — it selects which embedded SID frequency table
(`uii_hbplay_palfreq`/`uii_hbplay_ntscfreq`) and BPM timer delta get used from then on.

### `void uii_hbplay_init(unsigned char seq_start_pos, unsigned char play_mode)`

Full player (re-)initialization: resets all SID/UA working state, sets the song's
starting tempo, installs the tick IRQ at `$0314`/`$0315`, enables CIA1 Timer A and
a raster IRQ (for keyboard scanning independent of tempo), and starts playback from
sequencer step `seq_start_pos`.

`play_mode`: `0` = idle (registers still flush every tick, e.g. for envelope decay,
but no new rows are played), `1` = play the song. Can be called again at any time
to restart the song from a given step (see the test harness's `SPACE` binding).

### `void uii_hbplay_stop_all(void)`

Stops playback and resets SID working state (`uii_hbplay_state.play_mode = 0` +
re-runs the SID-side of init). Matches the original exactly: this does **not**
reset Ultimate Audio channel state (the reference player's own behavior — ported
as-is, not "fixed").

### `void uii_hbplay_set_tempo(unsigned char bpm_minus_64)`

Reprograms CIA1 Timer A from the embedded BPM table. `bpm_minus_64` = BPM − 64
(range 0–255 → 64–319 BPM). Applies the NTSC timer delta automatically if
`uii_hbplay_state.ntsc_detected`. Safe to call from within the tick IRQ (used internally by
the `Bt` track command) — preserves the caller's interrupt-enable state rather than
unconditionally re-enabling interrupts.

### `void uii_hbplay_play_fx(unsigned char ch, unsigned char sample, unsigned char note)`

Manually trigger `sample` (1–64, indexes `uii_hbplay_songdata.sample_params`) at `note`
(2–95; `0x26` = C-4 = 44100 Hz) on Ultimate Audio channel `ch` (0–6), independent of
song playback. Applies the sample's own note-pitch and transpose settings, exactly
as if it had been triggered from a pattern row. Safe to call from your main loop at
any time; internally disables/re-enables interrupts around the trigger.

### `void uii_hbplay_stop_fx(unsigned char ch)`

Stops the note on Ultimate Audio channel `ch` (0–6). If the channel is in
loop-with-release mode, releases the loop first rather than cutting immediately.

### `void uii_hbplay_fetch_pattern_row(void)`

Fetches the next pattern row into `uii_hbplay_row_buf` via REU streaming, advancing the
sequencer/pattern pointers as needed. Called internally by the tick dispatch; only
useful to call directly for diagnostics (e.g. printing raw row bytes).

### `void uii_hbplay_vis_reset(void)`

Clears the visualizer event queue (`uii_hbplay_vis_events[]`) and resets
`uii_hbplay_vis_event_count` to 0. Not called internally by the player — call it from your
own code when switching visualizer modes or restarting the song, if you want an
explicit clean slate.

### Visualizer hooks

Port of the original's `visualizerout` block — every SID and Ultimate Audio note
trigger appends a `(note, sound, channel, velocity)` event to a queue.

```c
#define UII_HBPLAY_VIS_MAX_EVENTS 32

typedef struct {
    unsigned char note;
    unsigned char sound;
    unsigned char velocity;  // 0-63 perceptual loudness estimate, not a raw register
    unsigned char channel;   // 0-6 = UA channels, 7-30 = SID channels
} uii_hbplay_vis_event_t;

extern uii_hbplay_vis_event_t uii_hbplay_vis_events[UII_HBPLAY_VIS_MAX_EVENTS];
extern unsigned char  uii_hbplay_vis_event_count; // valid entries since the last time YOUR code reset it
```

**`uii_hbplay_tick()` deliberately never resets this queue itself.** Ticks fire far
faster (~195 Hz) than any practical redraw rate (e.g. ~50 Hz for a once-per-VIC-
frame consumer), and notes only trigger a few times per pattern row — a per-tick
reset would wipe events before a slower consumer ever saw them. Instead, **your
own update cycle owns the reset**: each time you poll, take a snapshot of
`uii_hbplay_vis_event_count` first, process `uii_hbplay_vis_events[0 .. count-1]`, then set
`uii_hbplay_vis_event_count = 0` yourself (new events from ticks in between start
appending from index 0 again). See heartbeat-demo's `src/visualizer.c` (`vis_decay_and_draw()`)
for a complete, working reference implementation — it also
shows one way to build smooth VU-meter-style decay: keep a per-channel level
array, decay it a little every poll, and set it from whatever event(s) arrived
since the last poll.

SID channel numbers: `7 + sid_idx*3 + ch_idx` (so chip 0's 3 channels are 7, 8, 9;
chip 1's are 10, 11, 12; and so on). UA channels use their own index (0-6)
directly. `velocity` comes from the channel's ADSR envelope nibbles via a
perceptual-loudness heuristic (attack/decay/sustain/release weighted and clamped
to 0-63) — see `uii_hbplay_vis_adsr_weight()` in `ultimate_hbplay_lib.c` if you need the exact
formula.

`uii_hbplay_vis_sound` for Ultimate Audio channels is **not** the sample number directly —
the original reverses it (`sound_out = ~(sound_in - 1) & 0x3F, then +1`), a
display/palette convention with no further documented rationale, ported literally.
SID channels store the sample/instrument number as-is.

### Globals

| Symbol | Purpose |
|---|---|
| `uii_hbplay_songdata` | The loaded song's `$2000`-byte header (patterns/sequencer tables, sample/instrument parameter tables) — see [§4](#4-song-data-structures). |
| `uii_hbplay_state` | Player transport state (tempo, current row/pattern/sequencer position, mute tracking) — see [§5](#5-player-internal-state). |
| `uii_hbplay_row_buf[64]` | The current pattern row, as streamed from REU. |
| `uii_hbplay_sids[UII_HBPLAY_MAX_SIDS]` | Per-SID-chip working state (filter, 3× per-channel state) — see [§5](#5-player-internal-state). |
| `uii_hbplay_ua[UII_HBPLAY_UA_CHANNELS]` | Per-Ultimate-Audio-channel working state — see [§5](#5-player-internal-state). |
| `uii_hbplay_ext_out` | Sync-output byte written by the `Xt` command (nonzero param) — poll this from your own code to react to music-synced cues. Never cleared automatically. |
| `uii_hbplay_vis_events[UII_HBPLAY_VIS_MAX_EVENTS]` / `uii_hbplay_vis_event_count` | The visualizer event queue — see above. |

`UII_HBPLAY_MAX_SIDS` = 8, `UII_HBPLAY_UA_CHANNELS` = 7 (fixed by Ultimate Audio hardware),
`UII_HBPLAY_VIS_MAX_EVENTS` = 32.

---

## 4. Song Data Structures

These layouts are **dictated by the `.reu` file format** — byte offsets are exact
and must not be reordered. Traced from the original player's `SONGDATA` label
block and the `PlaySampleNote`/`ModulateChannel` record-field reads.

### Top-level song data (`uii_hbplay_songdata_t`, exactly `$2000` bytes)

| Offset | Field | Size | Notes |
|---|---|---|---|
| `$0000` | `sequencer_patterns[256]` | 256 | Pattern # per sequencer step (`$00`=end, `$FF`=loop, `$01`-`$40`=pattern) |
| `$0100` | `sequencer_transpose[256]` | 256 | Also the loop-target step # when `patterns[step]==$FF` |
| `$0200` | `sequencer_ultmutes[256]` | 256 | Bitmask, 7 UA channels, bit set = **not** muted |
| `$0300` | `sequencer_sidmutes[256]` | 256 | Bitmask, up to 8 SID chips, bit set = **not** muted |
| `$04E8` | `sid_volumes[8]` | 8 | Initial per-SID master volume |
| `$0540` | `starting_tempo` | 1 | BPM − 64 |
| `$0541` | `hardrestart_time` | 1 | Tick countdown value at which SID channels get a clean hard-restart ahead of the row's real note-on |
| `$0547` | `song_pattern_length` | 1 | Default pattern length (≤ 64) |
| `$0548` | `hardrestart_sr` | 1 | Envelope S/R applied during hard-restart |
| `$0549` | `hardrestart_ad` | 1 | Envelope A/D applied during hard-restart |
| `$054A` | `hardrestart_gateon_time` | 1 | Tick countdown value at which the early-gate-on pre-arm runs |
| `$054B` | `hardrestart_gateon_wave` | 1 | Waveform byte written during early-gate-on |
| `$0550` | `sid_addresses[16]` | 16 | 8 × (lo, hi) SID chip base addresses; `$0000` = chip slot unused |
| `$0800` | `sample_params[64]` | 2048 | 32 bytes/record — see below |
| `$1000` | `inst_params[64]` | 4096 | 64 bytes/record — see below |

Unmapped ranges (`$0400`–`$04E7`, `$04F0`–`$053F`, `$0560`–`$07FF`) are never read
by the player — editor-only metadata.

### `uii_hbplay_sample_params_t` — one Ultimate Audio sample record (32 bytes)

| Offset | Field |
|---|---|
| `$00`-`$0F` | (unused by the player — editor metadata) |
| `$10` | `volume` |
| `$11` | `pan` |
| `$12` | `note_pitch` — semitones; always applied. `transpose_now` (from `sequencer_transpose[256]`, see the top-level song data table above) is added on top of it unless the drum flag is set |
| `$13` | `finetune` (signed) |
| `$14` | `portamento` speed (`$00` = instant) |
| `$15` | `reu_bank` — sample REU address = `0x01000000 \| (reu_bank << 16)`; every sample begins at a 64 KB-aligned REU offset |
| `$16`-`$18` | `length[3]` — 24-bit, LSB-first |
| `$19`-`$1B` | `loop_a[3]` — 24-bit, LSB-first |
| `$1C`-`$1E` | `loop_b[3]` — 24-bit, LSB-first |
| `$1F` | `flags` — bit 7 = drum flag (suppresses transpose); bits 0-1 = loop mode: `0` = none, `1` = infinite loop (a key-up event stops the note immediately), `2` = loop with release (the first key-up event releases the loop and plays past the loop-B point to the end of the sample; a second key-up then stops the note immediately, as in mode 1), `3` = one-shot cropped to the loop A/B region (no actual looping) |

### `uii_hbplay_inst_params_t` — one SID instrument record (64 bytes)

| Offset | Field |
|---|---|
| `$00`-`$0F` | Instrument name (editor-only text, never read by the player) |
| `$10` | `env_ad` |
| `$11` | `env_sr` |
| `$12` | `finetune` (signed) |
| `$13` | `portamento` speed |
| `$14` | `pwm_start` — `$00` = don't reset PW (keep current sweep direction/value) |
| `$15` | `pwm_rate` |
| `$16` | `pwm_topbottom` — top/bottom nibbles |
| `$17` | `vib_delay` |
| `$18` | `vib_width` |
| `$19` | `vib_rate` |
| `$1A` | `filter_type` — `0` = no filter for this channel; bit 4 = cutoff-mod bounce-vs-stop flag |
| `$1B` | `filter_resonance` |
| `$1C` | `cutoff_init` — `0` = don't reset cutoff (keep current direction) |
| `$1D` | `cutoff_mod` — signed rate/direction |
| `$1E` | `cutoff_top` |
| `$1F` | `cutoff_bottom` |
| `$20`-`$2F` | `wave_table[16]` — waveform step table |
| `$30`-`$3F` | `arp_table[16]` — arpeggio step table (shares step indices with `wave_table`; `$FC`/`$FD`/`$FE` in the wave table are envelope-AD/envelope-SR/speed commands whose parameter comes from the matching arp-table slot; `$FF` is a loop-to-step command) |

---

## 5. Player-Internal State

Unlike §4, these structures are **freely designed** — not part of the file format,
re-initialized fresh at `uii_hbplay_init()`/`uii_hbplay_stop_all()`.

### `uii_hbplay_state_t`

| Field | Meaning |
|---|---|
| `play_mode` | `0`=idle, `1`=play song, bit 7 set = fully off (skips even register flush) |
| `tempo` | BPM − 64 |
| `tempo_ticks` | Ticks per row, from the BPM table |
| `tick` | Countdown to the next row |
| `patt_ptr` / `patt_bank` | Current pattern's REU offset/bank |
| `patt_length` / `patt_step` | Current pattern length and row index |
| `seq_start_pos` / `seq_step` | Sequencer position |
| `last_ua_mutes` / `last_sid_mutes` | Previous row's mute bitmasks, for mute-transition detection |
| `transpose_now` | Current sequencer step's transpose value |
| `ntsc_detected` | Set by `uii_hbplay_detect_ntsc()` |
| `reu_song_base` | REU address the song header was loaded to |

### `uii_hbplay_sid_chip_t` (× `UII_HBPLAY_MAX_SIDS`) and `uii_hbplay_sid_channel_t` (× 3 per chip)

Chip-level: filter cutoff (working value + sweep bounds/rate/bounce-flag),
`filt_ctrl` (per-channel filter-enable bits + resonance), `volume` (master vol +
filter-type nibble), `addr` (real SID chip address; `0` = unpopulated slot).

Per-channel: base frequency + portamento target/speed, vibrato (delay/phase/
width/rate/frac), PWM (rate + top/bottom bounds), finetune, instrument
pointer/wave-arp-table-stepping state, and the **active register image**
(`sid_freq`, `sid_pw`, `sid_wave`, `sid_env_ad/sr`) that gets flushed to real SID
hardware once per tick.

### `uii_hbplay_ua_channel_t` (× `UII_HBPLAY_UA_CHANNELS`)

Frequency + portamento target/speed, `shadow_gate` (the deferred control byte —
see [`section 9`](section 9#shadow-flush-pattern) for why only this one
field is deferred), finetune, note pitch/drum-flag/loop-mode, and the active
sample index.

---

## 6. Track Command Reference

A note byte with bit 7 set (in `uii_hbplay_row_buf`) is a track command, not a note. Its
low 7 bits select the command (`$00`-`$0A`); its parameter is the channel's own
"+1" byte (the same slot that normally holds the sample/instrument number). Cmd
`$0B`-`$7F` are undefined and ignored.

| # | Name | UA channel | SID channel | Cmd channel |
|---|------|:---:|:---:|:---:|
| `$00` | **Pa** — pan | set pan (0-15, immediate) | ignored | ignored |
| `$01` | **Bt** — tempo | `uii_hbplay_set_tempo(param)` | same | same |
| `$02` | **Dn** — slide down | portamento speed=`param`, target=`$0000` | same | slides **all** SID+UA channels toward `$0000` |
| `$03` | **Fi** — finetune | set finetune (signed) | same | ignored |
| `$04` | **Iv** — SID volume | applies to **all** SID chips (low nibble) | applies to **current chip only** | applies to all chips |
| `$05` | **Le** — pattern length | set (clamped to 64) | same | same |
| `$06` | **Co** — filter cutoff | ignored | set chip filter cutoff (`param<<5`, `\|0x8000`) | ignored |
| `$07` | **Po** — portamento speed | set speed only, no target change | same | ignored |
| `$08` | **Up** — slide up | portamento speed=`param`, target=`$17C0` | same | slides **all** SID+UA channels toward `$17C0` |
| `$09` | **Vo** — volume | set volume (0-63, immediate) | set envelope sustain nibble (`param<<4`) | ignored |
| `$0A` | **Xt** — sync/kill | nonzero param → `uii_hbplay_ext_out=param`; `0` → kill channel | same, or (Cmd channel) full re-init | nonzero → sync out; `0` → full re-init (`uii_hbplay_init_ua_and_sids()`) |

---

## 7. Firmware Prerequisites

An Ultimate 64 (any model) or C64 Ultimate with firmware 3.15 or newer, with
Ultimate Audio enabled (mapped at `$DF20`), a 16 MB REU, and the turbo registers
enabled (call `uii_turbo_fast()` before playing), plus the SID-specific settings from the
song's own `Heartbeat.cfg` (SID chip addressing, filter curve, audio mixer levels).
If a song plays but some SID channels are silent, check your U64's **Audio Mixer**
firmware settings (`F2` → Audio Mixer) — `Vol UltiSid 1`/`Vol UltiSid 2` must not be
`OFF`, and if physical SID sockets are populated and enabled, `$D400`/`$D420`
writes may route there instead of the internal UltiSID emulation (see
`SID Sockets Configuration` / `SID Addressing` in the firmware menu).

---

### Integration notes

The player takes over interrupts while it plays, and some of that reaches
into the KERNAL. heartbeat-demo's `main()` shows the full set-up:

- `uii_hbplay_init()` installs `uii_hbplay_irq` in the KERNAL IRQ vector
  (`$0314`/`$0315`) and reprograms CIA1 Timer A for the song's tempo; it is
  never restored. The CIA branch of the interrupt returns with its own RTI;
  the raster branch chains to the KERNAL at `$EA31` (keyboard scan), so the
  KERNAL ROM must be visible.
- With BASIC banked out (`$01 = $36`), the KERNAL's interrupt path jumps
  through `$0310` and through `$A002`/`$A003` into what is then RAM.
  heartbeat-demo writes an `RTS` to `$0310` and points `$A002`/`$A003` at it
  before starting; do the same in a program without BASIC ROM.
- Before calling `uii_hbplay_init()` the demo resets CIA1 Timer A to the
  normal 50 Hz rate (`cia1.ta = 0x4D25` on PAL) and ignores RESTORE with an
  empty NMI handler.
- Turbo must be on (`uii_turbo_fast()`): at 1 MHz a tick does not fit in
  its time slot.

---

## 8. Fidelity Notes and Known Limitations

- **Bit-exact by design, not accident.** Several routines look unusual in C
  (asymmetric bit-mask tables, direction-preserving sign logic, a fixed-point
  "$8000-based" internal representation for filter cutoff and pulse width) because
  they're literal translations of specific 6502 self-modifying-code or
  carry-propagation tricks in the original — see the comment above each such
  function in `ultimate_hbplay_lib.c` for the exact source line reference.
- **`uii_hbplay_stop_all()` does not reset Ultimate Audio state** — matches the original's
  own `StopAllSound`, which only re-runs the SID-side init.
- **`MusicPlayMode == 2`** (pattern-loop/editor live-preview mode) is not
  implemented — the standalone player itself never uses it either.
- **Visualizer output hooks** are ported (see [§3](#visualizer-hooks) above) with
  one deliberate deviation from the original: the event queue accumulates across
  ticks instead of resetting every tick, since a real consumer polls far slower
  than the tick rate. This module only exposes the
  raw hooks -- heartbeat-demo's `src/visualizer.c` (VU bars, plasma,
  spectroscope, sprite scroller) is a complete consumer built on them, usable
  as a reference for your own.
- PWM/filter/cutoff internal representations use the same "$8000 marker bit,
  hardware ignores the unused high bits" trick as the original — see
  section 9 if you need to read or modify these fields directly instead of
  through the public API.

---

## 9. Internals

Adapted from heartbeat-demo's `ARCHITECTURE.md` (where the port was written and verified); file names refer to this library.

### IRQ / Tick Architecture

#### Dual dispatch: raster vs. CIA1 Timer A

`uii_hbplay_init()` installs `uii_hbplay_irq` at `$0314`/`$0315` and enables **both**:

- A fixed-line **raster IRQ** (line 0), independent of tempo — its only job is
  guaranteed ~50/60 Hz keyboard scanning, so the keyboard stays responsive
  regardless of how slow the current BPM's tick rate is.
- **CIA1 Timer A**, reprogrammed by `uii_hbplay_set_tempo()` from the embedded BPM table —
  this is what actually drives `uii_hbplay_tick()`.

`uii_hbplay_irq` (a raw, prologue-free `__asm` block — not a C function; installed via
`*((void**)0x0314) = uii_hbplay_irq`, never called with `uii_hbplay_irq()`) checks `$D019` bit 0
first to tell the two apart, and the two branches deliberately end differently:

```
uii_hbplay_irq:
    lda $d019
    and #$01
    bne uii_hbplay_irq_raster        ; VIC raster IRQ -> just ack + chain

    lda $02                  ; save/restore ZP $02 around the call -- see below
    pha
    jsr uii_hbplay_tick
    pla
    sta $02
    lda $dc0d                ; ack CIA1 Timer A IRQ (clear-on-read)
    pla                      ; restore A/X/Y in the order the KERNAL's own
    tay                      ; hardware IRQ entry ($FF48) pushed them, and
    pla                      ; RTI directly -- bypasses the KERNAL IRQ tail
    tax                      ; entirely, so this branch never touches
    pla                      ; SCNKEY or the jiffy-clock/STOP-key logic
    rti

uii_hbplay_irq_raster:
    sta $d019                ; ack raster IRQ
    jmp $ea31                ; $EA31's own sequence includes SCNKEY
```

Only the raster branch chains to KERNAL `$EA31`, whose own sequence includes
`SCNKEY` — this is the only place keyboard scanning happens, at the raster
IRQ's fixed ~50/60 Hz rate, independent of however slow the current BPM's
tick rate is. The CIA1 (tick) branch fires far too often (~195 Hz) for
keyboard scanning or KERNAL's jiffy-clock/STOP-key logic to run there, so it
bypasses the KERNAL tail entirely with its own register-restore-and-`rti`.

#### The `__interrupt` auto-save gap

`uii_hbplay_tick` is declared `__interrupt`, so Oscar64 auto-generates a save/restore
prologue for whatever zero-page locations it can **statically** determine the
function's call tree touches (currently: `WORK+0..3`, `P0`-`P10`, `ACCU+0..3`,
`T0`-`T3`, and `$4D`-`$51`).

This analysis has a real, confirmed gap: **`mul16by8`** (Oscar64's runtime
multiply helper, pulled in by several `note << 6`-style computations across the
call tree) uses zero-page **`$02`** as scratch, and `$02` is never part of the
auto-saved set. Left unprotected, this silently corrupts `$02` for whatever else
the KERNAL/other code (running outside the tick IRQ) is using it for. `uii_hbplay_irq`
protects it manually with a `pha`/`pla` pair around the `jsr uii_hbplay_tick`, `$02` chosen
specifically because it's what the sweep in the next section found.

#### Zero-page gap analysis methodology

This isn't a one-time fix — any change to `uii_hbplay_tick`'s call tree (a new runtime-
helper dependency, a new function it calls into) can introduce a new gap. Re-run
this method whenever that call tree changes:

1. Build with `-g` (`make check` builds `build/compile_hbplay64.prg`; add `-g` to
   get `build/compile_hbplay64.asm` with source-annotated disassembly).
2. Locate `uii_hbplay_tick`'s auto-save prologue in the `.asm` output — a run of
   `LDA <addr> / PHA` pairs at the top of the function — to get the exact
   currently-auto-saved set.
3. Find `uii_hbplay_tick`'s real address range (next top-level label after it) and collect
   every `JSR` target inside it.
4. **Recursively repeat for every JSR target's own address range** (using the full
   label list, not just `uii_hbplay_`-prefixed ones — runtime library routines like
   `mul16by8`/`divmod`/`divmod32` sit in the same address space and must be
   included). Watch for wrong range boundaries: picking the *next* label instead of
   the function's *actual* end pulls in unrelated code that happens to be laid out
   next in the binary and produces false-positive "JSR"s that don't belong to the
   call tree at all — this has happened in practice, so double-check range
   boundaries against the disassembly rather than assuming the next label is right.
5. For each function in the resulting reachable set, grep its disassembly for raw
   zero-page addressing (`LDA $XX` / `STA $XX` with a **bare 2-hex-digit operand,
   no `WORK`/`ACCU`/`P`/`T`-prefixed symbol name and no `#` immediate marker**) —
   that's the signature of an *unnamed* zero-page location Oscar64's own register
   allocator isn't tracking as part of the auto-save set.
6. Cross-reference every such location against the current auto-save set. Anything
   not covered needs manual protection in `uii_hbplay_irq`, the same way `$02` is.

This sweep has found exactly one gap throughout the whole port ($02, `mul16by8`),
also after the move into this library (re-checked for 1.4.0: the tick's call
tree is 24 functions, runtime helpers `mul16by8` and `divmod`)
— `divmod`/`divmod32` (used by the octave-fold frequency lookups, see
[§6](#6-frequency-conversion)) only ever touch `WORK`/`ACCU`, which are already
covered.

---

### Shadow-Flush Pattern

SID and Ultimate Audio use **different** deferred-write strategies, both ported
exactly from the original:

**SID**: every channel's full active-register image (`sid_freq`, `sid_pw`,
`sid_wave`, `sid_env_ad`/`sr`) is computed into `uii_hbplay_sid_channel_t` fields
throughout the tick (by `uii_hbplay_play_sid_note()` at trigger time, then continuously
recomputed by `uii_hbplay_modulate_channel()`), and only actually **written to hardware**
once, at the very end of the tick, by `uii_hbplay_write_one_sid()`
(`uii_hbplay_register_update_sid()`'s per-chip helper). This batches all SID register
writes together for tight sync — every channel's frequency/waveform/envelope
changes land in the same "frame."

**Ultimate Audio**: only the gate/control byte (`shadow_gate`: `0x00`/`0x10`/
`0x11`/`0x13`) is deferred. Sample start address, length, loop points, volume,
pan, and playback rate are all written **directly to hardware immediately** at
trigger time (`uii_hbplay_trigger_sample()`) or every tick (`uii_hbplay_modulate_ua_channel()`'s
rate recompute) — there is no shadow/batch mechanism for these in the original,
so none was added here. Only the final "start/stop/loop" trigger byte needs to be
deferred, and `uii_hbplay_register_update_ua()` flushes all 7 channels' `shadow_gate` to
`UAControl` at the same point in the tick as the SID flush.

---

### Frequency Conversion

`uii_hbplay_get_sid_freq()`/`uii_hbplay_get_ultimate_freq()` convert a 16-bit "linear" frequency
(note × 64, an internal fixed-point unit used throughout — see
[§7](#7-fixed-point-internal-representations)) into the real hardware register
value, via **octave-folding + a per-octave lookup table**:

1. Fold the input down by subtracting 3 from the high byte repeatedly until it's
   below `$18` (24) — this maps any octave onto one of 8 "octave slots" within the
   embedded frequency table.
2. `shift = 7 - (octave_slot / 3)` for SID, `shift = octave_slot / 3` for Ultimate
   Audio (**reversed** — verified against source, not a typo) — the number of bits
   to right-shift the looked-up 16-bit table value by.
3. `page = (octave_slot % 3) * 256` selects one of 3 sub-tables within the octave
   slot's 768-byte block.
4. Look up the 16-bit value at `table[page + x]` (low byte) /
   `table[page + 0x300 + x]` (high byte, tables offset +3 pages from the low-byte
   tables), combine, and shift right by `shift`.
5. Ultimate Audio's version applies one more step: subtract 1 from the final
   result (a plain unsigned 16-bit `-= 1` already wraps/borrows correctly, no
   separate borrow-flag logic needed in C).

The original implements this via **self-modifying code** — patching a `JMP`
target's low byte to select the shift amount, and an `LDA` operand's high byte to
select the table page. The C port re-expresses this as ordinary indexed array
access + a runtime shift, verified mathematically equivalent by working through
both derivations and confirming the same bit positions result (both use the
`divmod` runtime helper for the `/3`/`%3`, already covered by the zero-page
auto-save set — see [§3](#3-irq--tick-architecture)).

---

### Fixed-Point Internal Representations

Two working-state fields use a shared "`$8000`-based" 16-bit representation,
carried over directly from the original rather than normalized to a plain 0-based
range:

- **SID filter cutoff** (`uii_hbplay_sid_chip_t.filter_lo/hi`): initialized to
  `0x8000 | (cutoff_init << 5)`.
- **SID pulse width** (`uii_hbplay_sid_channel_t.sid_pw_lo/hi`): initialized to
  `0x8000 | (pwm_start << 4)`.

The `0x8000` bit isn't arithmetically meaningful — the real SID hardware registers
for both of these are less than 16 bits wide (filter cutoff is 11 bits, pulse
width is 12 bits), and the high bits above that range are simply **ignored by the
SID chip itself** when the value is written directly to the hardware register (no
masking needed on write — confirmed against the SID datasheet's register bit
widths). The marker bit's only real purpose is letting arithmetic like "clamp to a
top/bottom bound" and "check if a value is populated" work with ordinary unsigned
16-bit comparisons on values that would otherwise sit awkwardly close to the
sign/zero boundary. When reading these fields directly (bypassing the public API),
mask off the top bits explicitly rather than assuming a 0-based range.

---

