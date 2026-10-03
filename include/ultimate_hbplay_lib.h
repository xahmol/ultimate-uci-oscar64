/*****************************************************************
Heartbeat Soundtracker player -- C/Oscar64 port
ultimate-uci-oscar64 -- https://github.com/xahmol/ultimate-uci-oscar64

Plays a Heartbeat Soundtracker song (.reu file, loaded into the REU via
the UCI) on up to UII_HBPLAY_MAX_SIDS SID chips (addresses from the song
data) and all 7 Ultimate Audio DMA channels. C64 on an Ultimate 64 / C64
Ultimate only: needs Ultimate Audio, the REU and turbo mode (see
docs/HEARTBEATPLAYER_MANUAL.md for the firmware settings).

Based on the Heartbeat Soundtracker standalone player by Aleksi Eeben /
Eight Bit Shed (https://sites.google.com/view/heartbeatsoundtracker).
Ported to C by Xander Mol for heartbeat-demo, moved into this library in
1.4.0. Used and published with the author's written permission, including
his lookup tables (include/heartbeat/): see NOTICE.md.

Takes over the KERNAL IRQ vector ($0314) and CIA1 Timer A while playing;
uses ultimate_audio_lib, ultimate_common_lib and ultimate_dos_lib.
******************************************************************/

#ifndef _ULTIMATE_HBPLAY_LIB_H_
#define _ULTIMATE_HBPLAY_LIB_H_

// ---------------------------------------------------------------
// Song data layout — DICTATED BY THE HEARTBEAT FILE FORMAT.
// Byte offsets below are exact; do not reorder fields. Traced from
// the original player.s lines 138-160 (top-level
// offsets) and PlaySampleNote / ModulateChannel (record layouts).
// ---------------------------------------------------------------

// One 32-byte SAMPLEPARAMS record (up to 64 of these, at SONGDATA+$0800)
typedef struct {
    unsigned char _unused[0x10];   // $00-$0F — never read by the player
    unsigned char volume;          // $10
    unsigned char pan;             // $11
    unsigned char note_pitch;      // $12 — semitones; always applied. Sequencer transpose
                                    // is added on top of it unless the drum flag is set
    unsigned char finetune;        // $13 — signed
    unsigned char portamento;      // $14 — $00 = instant
    unsigned char reu_bank;        // $15 — REU sample address = 0x01000000 | (reu_bank << 16)
    unsigned char length[3];       // $16-$18 — 24-bit, LSB-first
    unsigned char loop_a[3];       // $19-$1B — 24-bit, LSB-first
    unsigned char loop_b[3];       // $1C-$1E — 24-bit, LSB-first
    unsigned char flags;           // $1F — bit7 = drum flag (suppresses transpose); bits0-1 =
                                    // loop mode: 0=none, 1=infinite (key-up stops immediately),
                                    // 2=loop-with-release (first key-up releases, plays to end
                                    // of sample; second key-up then stops immediately),
                                    // 3=one-shot cropped to loop A/B region
} uii_hbplay_sample_params_t;              // 32 bytes

// One 64-byte INSTPARAMS record (up to 64 of these, at SONGDATA+$1000).
// $00-$0F is the instrument's NAME (editor-only text, never read by the
// player) -- not step data. The real wave/arp tables are at $20-$2F/$30-$3F,
// derived from ModulateChannel's actual indexing (`and #$0f; ora #$30` ->
// arp offset $30-$3F; `eor #$10` -> wave offset $20-$2F).
typedef struct {
    unsigned char _name[0x10];          // $00-$0F — instrument name (editor-only, never read by the player)
    unsigned char env_ad;               // $10
    unsigned char env_sr;                // $11
    unsigned char finetune;              // $12 — signed
    unsigned char portamento;            // $13
    unsigned char pwm_start;             // $14 — $00 = continue current direction
    unsigned char pwm_rate;              // $15
    unsigned char pwm_topbottom;         // $16 — top/bottom nibbles
    unsigned char vib_delay;             // $17
    unsigned char vib_width;             // $18
    unsigned char vib_rate;              // $19
    unsigned char filter_type;           // $1A — 0 = no filter
    unsigned char filter_resonance;      // $1B
    unsigned char cutoff_init;           // $1C — 0 = no init
    unsigned char cutoff_mod;            // $1D — signed rate/direction
    unsigned char cutoff_top;            // $1E
    unsigned char cutoff_bottom;         // $1F
    unsigned char wave_table[0x10];      // $20-$2F — waveform step table (16 steps)
    unsigned char arp_table[0x10];       // $30-$3F — arpeggio step table (16 steps)
} uii_hbplay_inst_params_t;                      // 64 bytes

// The full $2000-byte song-data blob, streamed from REU (fixed REU source
// offset $00E000 per main.s) into C64 RAM via one uii_audio_reu_fetch() call.
typedef struct {
    unsigned char sequencer_patterns[256];  // $0000 — pattern # per step ($00=end,$FF=loop,$01-$40=pattern)
    unsigned char sequencer_transpose[256]; // $0100 — also loop-target step # when patterns[step]==$FF
    unsigned char sequencer_ultmutes[256];  // $0200 — bitmask, 7 UA channels
    unsigned char sequencer_sidmutes[256];  // $0300 — bitmask, SID channels
    unsigned char _pad1[0x04E8 - 0x0400];   // $0400-$04E7 — unused
    unsigned char sid_volumes[8];           // $04E8 — initial per-SID volume
    unsigned char _pad2[0x0540 - 0x04F0];   // $04F0-$053F — unused
    unsigned char starting_tempo;           // $0540
    unsigned char hardrestart_time;         // $0541
    unsigned char _pad3[0x0547 - 0x0542];   // $0542-$0546 — unused
    unsigned char song_pattern_length;      // $0547
    unsigned char hardrestart_sr;           // $0548
    unsigned char hardrestart_ad;           // $0549
    unsigned char hardrestart_gateon_time;  // $054A
    unsigned char hardrestart_gateon_wave;  // $054B
    unsigned char _pad4[0x0550 - 0x054C];   // $054C-$054F — unused
    unsigned char sid_addresses[16];        // $0550 — 8 x (lo,hi); 0000 = chip slot unused
    unsigned char _pad5[0x0800 - 0x0560];   // $0560-$07FF — unused
    uii_hbplay_sample_params_t sample_params[64];   // $0800
    uii_hbplay_inst_params_t   inst_params[64];     // $1000
} uii_hbplay_songdata_t;                            // must be exactly 0x2000 bytes

// NOTE: the classic C "static assert via negative array size" idiom does
// NOT work on Oscar64 — confirmed by deliberately breaking the size and
// rebuilding: no error, no warning. Oscar64's array-bound check does not
// reject negative/absurd sizes in this context (even with a real instance
// declared, not just a typedef). If this struct's layout is ever changed,
// verify sizeof(uii_hbplay_songdata_t) at runtime (e.g. a one-off printf) rather
// than trusting a compile-time check -- see the Oscar64 manual's gotcha list.

#define UII_HBPLAY_SONGDATA_SIZE  0x2000UL
#define UII_HBPLAY_SONGDATA_REU_SRC_DEFAULT_OFFSET 0x00E000UL // fixed offset within the .reu file's REU image, per main.s

// Suggested REU base address for uii_hbplay_load() when only one song is
// resident at a time (heartbeat-demo reloads every song to this address).
#define UII_HBPLAY_SONG_REU_BASE  0x000000UL

// ---------------------------------------------------------------
// Player-internal working state — freely designed (NOT part of the file
// format; re-initialized fresh at uii_hbplay_init(), matching InitSIDImageAndVolumes).
// ---------------------------------------------------------------

#define UII_HBPLAY_MAX_SIDS       8   // matches sid_addresses[] capacity
#define UII_HBPLAY_UA_CHANNELS    7   // fixed by Ultimate Audio hardware

typedef struct {
    unsigned char play_mode;      // 0=idle/update-sounds-only, 1=song play, 0x80=off
    unsigned char tempo;          // PlayerTempo (BPM-64)
    unsigned char tempo_ticks;    // TempoTicks: ticks per row
    unsigned char tick;           // Tick: countdown to next row
    unsigned      patt_ptr;       // PattPointer/H: REU offset within pattern bank
    unsigned char patt_bank;      // PattPointerB: REU bank for current pattern
    unsigned char patt_length;    // PatternLength
    unsigned char patt_step;      // PatternStep
    unsigned char seq_start_pos;  // SeqStartPos
    unsigned char seq_step;       // SequencerStep
    unsigned char last_ua_mutes;  // LastUltMutes
    unsigned char last_sid_mutes; // LastSIDMutes
    signed char   transpose_now;  // TransposeNow
    unsigned char ntsc_detected;  // 1 = NTSC timing/frequency tables in use
    unsigned long reu_song_base;  // REU address the current song's header was loaded to
} uii_hbplay_state_t;

extern uii_hbplay_state_t uii_hbplay_state;

// RowBuffer equivalent — one pattern row (streamed per-row via reu_fetch).
// $00-$0D = 7 UA channels x 2 bytes (note, sample#)
// $0E-$3D  = up to UII_HBPLAY_MAX_SIDS x 3 channels x 2 bytes (note, sound#)
// $3E-$3F (62-63) = Cmd channel, 2 bytes (command, param) like every
// other channel -- confirmed from PlayPatternRow's Cmd-channel check,
// which leaves Y=62 set when calling the command handler, so its
// "RowBuffer+1,y" parameter read lands on byte 63.
extern unsigned char uii_hbplay_row_buf[64];

// Per-SID-channel working state (x3 per chip). Kept byte-for-byte parallel
// to the original's zero-page/parameter-page fields (not combined into
// wider ints) so uii_hbplay_modulations() can mirror the source's 6502 carry
// propagation exactly instead of re-deriving it.
typedef struct {
    unsigned char base_freq_lo, base_freq_hi;      // BaseFreq/H
    unsigned char vib_delay, vib_phase, vib_width; // vibrato
    unsigned char vib_rate, vib_rate_hi;           // VibRate/H
    unsigned char vib_frac;                        // VibFrac
    unsigned char pwm_rate, pwm_rate_hi;            // PWMRate/H
    unsigned char pwm_top_hi, pwm_bottom_hi;        // PWMTopH/PWMBottomH
    unsigned char finetune, finetune_hi;            // SIDFineTune/H
    unsigned char gate_mask;                        // SIDGate
    unsigned char instr_lo, instr_hi;               // SIDInstr/H (index into inst_params, 0 = none)
    unsigned char wave_arp_step, wave_arp_count, wave_arp_speed;
    unsigned char current_arp;                      // SIDCurrentArp
    unsigned char target_freq_lo, target_freq_hi;   // SIDTargetFreq/H (portamento)
    unsigned char portamento;                       // SIDPortamento

    // Active register image (SIDImage in the original) -- what
    // WriteOneSID actually flushes to hardware each tick. Distinct from
    // the working-parameter fields above: those feed into these via
    // uii_hbplay_modulations(); uii_hbplay_play_sid_note() sets these directly/statically
    // at note-trigger time as a simplification.
    unsigned char sid_freq_lo, sid_freq_hi;         // SIDFreq/H
    unsigned char sid_pw_lo, sid_pw_hi;              // SIDPW/H
    unsigned char sid_wave;                          // SIDWave (waveform | gate)
    unsigned char sid_env_ad, sid_env_sr;            // SIDEnvAD/SR
} uii_hbplay_sid_channel_t;

typedef struct {
    unsigned char filter_lo, filter_hi;             // SIDFilter/H ($8000-$9FFF internal repr)
    unsigned char filt_ctrl;                        // SIDFiltCtrl
    unsigned char volume;                            // SIDVolume (hi nybble filter type, lo nybble vol)
    unsigned char cutoff_mod;                        // signed
    unsigned char cutoff_top_lo, cutoff_top_hi;
    unsigned char cutoff_bottom_lo, cutoff_bottom_hi;
    unsigned char cutoff_bounce;
    unsigned      addr;                              // SID chip base address; 0 = inactive slot
    uii_hbplay_sid_channel_t ch[3];
} uii_hbplay_sid_chip_t;

extern uii_hbplay_sid_chip_t uii_hbplay_sids[UII_HBPLAY_MAX_SIDS];

// Per-UA-channel working state (x7). shadow_gate is the deferred control
// byte flushed to hardware once per tick by RegisterUpdate's UA half —
// the whole shadow-flush design hinges on this field.
typedef struct {
    unsigned char freq_lo, freq_hi;                 // UAFreq/H
    unsigned char target_freq_lo, target_freq_hi;   // UATargetFreq/H (portamento)
    unsigned char shadow_gate;                       // deferred UAControl: 0x00/0x10/0x11/0x13
    unsigned char finetune_lo, finetune_hi;          // UAFineTune/H
    unsigned char note_pitch;                        // UANotePitch
    unsigned char drum_flag;                         // UADrumFlag
    unsigned char loop_mode;                         // UALoopMode
    unsigned char portamento;                        // UAPortamento
    unsigned char last_volume, last_pan;              // unused MVP; parity w/ visualizer fields
    unsigned char sample_lo, sample_hi;               // UASample/H (index into sample_params, 0 = none)
} uii_hbplay_ua_channel_t;

extern uii_hbplay_ua_channel_t uii_hbplay_ua[UII_HBPLAY_UA_CHANNELS];

// ---------------------------------------------------------------
// Public API — mirrors player.s's public surface.
// ---------------------------------------------------------------

char uii_hbplay_load(char *filename, unsigned long reu_addr);
// Open `filename` in the current UCI directory (else in the UCI home
// directory), load the whole file into the REU at `reu_addr` in 32767-byte
// chunks (uii_load_reu_at), close it, then uii_audio_reu_fetch() the
// $2000-byte song-data header into uii_hbplay_songdata. Sets
// uii_hbplay_state.reu_song_base. To load from somewhere else, change
// directory first (uii_change_dir, or uii_scan_media/uii_find_media_path to
// search the drives, as heartbeat-demo does).
// Input:  filename — .reu file name (ASCII, as the UCI expects)
//         reu_addr — REU base address to load the file into (e.g.
//                    UII_HBPLAY_SONG_REU_BASE)
// Output: 1 on success, 0 if the file could not be found or loaded
// Syntax: char ok = uii_hbplay_load("My Song.reu", UII_HBPLAY_SONG_REU_BASE);

char uii_hbplay_detect_ntsc(void);
// Raster-line PAL/NTSC detection (DetectNTSC port). Sets uii_hbplay_state.ntsc_detected.
// Call once at startup, before uii_hbplay_init().
// Input:  none
// Output: 1 if NTSC timing detected, 0 if PAL; also sets uii_hbplay_state.ntsc_detected
// Syntax: uii_hbplay_detect_ntsc(); uii_hbplay_init(0, 1);

void uii_hbplay_init(unsigned char seq_start_pos, unsigned char play_mode);
// PlayerInit port: reset player state, init SID images/volumes and UA
// channels, set starting tempo, and install the tick IRQ at $0314/$0315.
// Call uii_hbplay_detect_ntsc() before this.
// Input:  seq_start_pos — sequencer step to start playback from (usually 0)
//         play_mode     — 0 = idle (registers still flush every tick, no new
//                         rows played), 1 = play the song
// Output: none
// Syntax: uii_hbplay_init(0, 1); // start playing from the beginning

void uii_hbplay_stop_all(void);
// StopAllSound port: stop playback, silence all SIDs/UA channels.
// Input:  none
// Output: none
// Syntax: uii_hbplay_stop_all();

void uii_hbplay_set_tempo(unsigned char bpm_minus_64);
// SetTempo port: A = BPM-64 (0-255 -> 64-319 BPM). Reprograms CIA1 Timer A
// from the embedded BPM table, applying the NTSC delta if uii_hbplay_state.ntsc_detected.
// Input:  bpm_minus_64 — desired tempo, encoded as BPM-64
// Output: none
// Syntax: uii_hbplay_set_tempo(60); // BPM = 60+64 = 124

void uii_hbplay_play_fx(unsigned char ch, unsigned char sample, unsigned char note);
// PlayFX port: play `sample` (1-64) at `note` (2-95, C-4=0x26) on UA channel ch (0-6).
// Input:  ch     — Ultimate Audio channel, 0-6
//         sample — sample/instrument number, 1-64 (indexes uii_hbplay_songdata.sample_params)
//         note   — note pitch, 2-95 (0x26 = C-4 = 44100 Hz)
// Output: none
// Syntax: uii_hbplay_play_fx(6, 1, 0x26); // play sample 1 at C-4 on channel 6

void uii_hbplay_stop_fx(unsigned char ch);
// StopFX port: stop note on UA channel ch (0-6); releases loop first if loop mode 2.
// Input:  ch — Ultimate Audio channel, 0-6
// Output: none
// Syntax: uii_hbplay_stop_fx(6);

void uii_hbplay_fetch_pattern_row(void);
// FetchPatternRow port: fetches the next pattern row into uii_hbplay_row_buf via
// uii_audio_reu_fetch(), advancing the sequencer/pattern pointers as needed. Called
// internally by uii_hbplay_tick's dispatch (ahead of a hard-restart); exposed here
// mainly for diagnostics (e.g. printing raw row bytes).
// Input:  none
// Output: none (result written to uii_hbplay_row_buf[64])
// Syntax: uii_hbplay_fetch_pattern_row(); // then inspect uii_hbplay_row_buf[] directly

extern uii_hbplay_songdata_t uii_hbplay_songdata;

// Xt track command's sync-output byte (ExtOut in the original): the Xt
// command with a nonzero parameter writes that byte here instead of
// killing the channel -- intended for a demo to poll and react to
// music-synced cues. Never cleared automatically; matches the original.
extern unsigned char uii_hbplay_ext_out;

// ---------------------------------------------------------------
// Visualizer hooks -- port of player.s's "visualizerout" block
// (SIDNote/SIDVelocity/SIDHalfVelocity/SampleNote/SampleVelocity/Reset).
// Populates a queue of (note, sound, channel, velocity) tuples every time
// a note triggers on any SID or Ultimate Audio channel.
//
// UNLIKE the original's VisualizerFrameInit (which resets the queue at
// the start of every TICK, ~195 Hz -- correct there because the original's
// visualizer is assumed to consume it synchronously within the same
// tick), THIS port's uii_hbplay_tick() never resets the queue itself: events
// accumulate (wrapping back to index 0 past UII_HBPLAY_VIS_MAX_EVENTS) until
// whoever is reading it -- typically a main-loop visualizer polling once
// per VIC frame, i.e. roughly once every 4 ticks -- resets
// uii_hbplay_vis_event_count back to 0 after taking its own snapshot. Resetting
// every tick as the original does would wipe out almost every event
// before a ~50 Hz poll ever saw it, since notes only trigger roughly once
// per pattern row (~every 20-25 ticks), not every tick. See
// heartbeat-demo's src/visualizer.c for a reference consumer: snapshot `count = uii_hbplay_vis_event_count`, immediately
// reset `uii_hbplay_vis_event_count = 0`, then process `uii_hbplay_vis_events[0..count-1]`.
//
// Channel numbering matches the original: 0-6 = Ultimate Audio channels
// (direct channel index), 7-30 = SID channels (7 + sid_idx*3 + ch_idx).
// `velocity` is a 0-63 perceptual loudness estimate derived from the
// channel's ADSR envelope (see ultimate_hbplay_lib.c's uii_hbplay_vis_adsr_weight()), not a
// literal register value.
// ---------------------------------------------------------------
#define UII_HBPLAY_VIS_MAX_EVENTS 32

typedef struct {
    unsigned char note;
    unsigned char sound;
    unsigned char velocity;
    unsigned char channel;
} uii_hbplay_vis_event_t;

extern uii_hbplay_vis_event_t uii_hbplay_vis_events[UII_HBPLAY_VIS_MAX_EVENTS];
extern unsigned char uii_hbplay_vis_event_count; // valid entries in uii_hbplay_vis_events since the last consumer reset

void uii_hbplay_vis_reset(void);
// Clears the entire event queue and resets the count to 0. Not called
// internally by the player (matches the original -- VisualizerReset has
// no internal caller in player.s either) -- for a visualizer to call e.g.
// when switching modes or restarting the song.
// Input:  none
// Output: none
// Syntax: uii_hbplay_vis_reset();

#pragma compile("ultimate_hbplay_lib.c")

#endif
