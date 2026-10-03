# Changelog

All notable changes to ultimate-uci-oscar64. Versions follow
[semantic versioning](https://semver.org/): MAJOR for incompatible API
changes (renamed or removed functions, changed parameters), MINOR for new
functions, PATCH for fixes that keep the API.

## [1.4.0] - 2026-10-03

### Added

- **Heartbeat Soundtracker player** (`ultimate_hbplay_lib`,
  `docs/HEARTBEATPLAYER_MANUAL.md`): plays Heartbeat Soundtracker songs
  (`.reu`) on up to 8 SID chips and the 7 Ultimate Audio channels, with
  per-tick modulation (vibrato, pulse width, filter sweep, wave/arpeggio
  tables, portamento), all 11 track commands, sound effects
  (`uii_hbplay_play_fx`/`stop_fx`), PAL/NTSC detection, music sync output
  and visualizer hooks. **Based on the Heartbeat Soundtracker player by
  Aleksi Eeben / Eight Bit Shed**; the C port was written for heartbeat-demo
  and moved here, now on the library's audio and DOS functions. The player's
  own tempo and frequency tables are included (`include/heartbeat/`), with
  his written permission: see `NOTICE.md`. Section hook
  `UII_HBPLAY_CODE`/`_DATA`/`_BSS`; `uii_hbplay_load()` loads from the
  current or home directory (the demo's drive search stays in programs).
- `tests/hbplay_test.c` (`make hbplaytest`): hardware test of the player;
  `tests/compile_hbplay.c` in `make check`.

### Fixed (documentation)

- README: the section hook (1.3.0) is no longer listed as a missing
  feature.

## [1.3.0] - 2026-10-03

### Added

- **Upic module** (`ultimate_upic_lib`, `docs/UPIC_MANUAL.md`): Aleksi
  Eeben's 384x256, 16-color Upic picture mode. Display with an exact
  one-dot pixel pitch on both the 64 MHz path (Elite II / C64 Ultimate,
  every 8th pixel pair from a per-line patched immediate, after Aleksi's
  Upic v1.3) and the 48 MHz path (Ultimate 64 / Elite I, 3 of every 4
  pixels, after Christian Gleissner's mandelbrot-upic PR #2, with every
  4th group patched), chosen automatically with `uii_turbo_probe_max()`.
  The line renderer is generated at run time from the column addresses,
  so any layout works (`UII_UPIC_BITMAP`, `UII_UPIC_RELOC_COLS`,
  `UII_UPIC_RELOC_BASE`). Polled frames and Aleksi's raster-IRQ viewer
  (with a per-frame hook), both limited to a display window of rows
  with `uii_upic_set_window()` (e.g. a progress band while the rest of
  the CPU time goes to computing); drawing (plot, read pixel, clear, 8x8
  text, hex); `.upic` save and load in the locked Upic v1.3 format
  (256-byte header with palette, text, UI colours and time stamp),
  loading the converter's bare bitmaps too.
  **Code contributed by Aleksi Eeben**, creator of Upic: the display,
  IRQ viewer and drawing routines are based on his Upic v1.3 source
  (`display.s`, `drawing.s`), which he shared and allowed to be
  published here (2026-10-03; his ActualPlot only, without Upic Paint's
  tool-panel check), and the file format is his. The 48 MHz path is
  Christian Gleissner's.
- `uii_sendcommand_data()` and `uii_readdata_to()`: send a command whose
  payload comes straight from memory, and read a reply straight into
  memory, without the shared command buffer or `uii_data[]`.
- `uii_write_file_from()` and `uii_read_file_to()`: file I/O on top of
  those (no heap, no large data queue); `uii_read_file_to()` follows the
  firmware's 512-byte packets.
- Section hook for every module (`UII_<MODULE>_CODE`/`_DATA`/`_BSS` on the
  compiler command line): place library code in project sections without
  editing the submodule (issue #1).
- `tests/upic_test.c` (`make upictest`): hardware test of the Upic module
  and the streaming file I/O.

### Fixed (documentation)

- File open attribute `0x0E` does not overwrite an existing file: with
  `FA_CREATE_NEW` set the firmware (FatFS) answers `FILE EXISTS`. Overwrite
  is `0x0A` (`FA_WRITE | FA_CREATE_ALWAYS`).

## [1.2.1] - 2026-10-03

### Fixed

- `uii_load_config()` now sends the file name with its terminating 0. The
  firmware reads the name as a C string and doesn't terminate incoming
  commands, so without it the name ran on into bytes left over from an
  earlier, longer command: the call worked or answered `88,CANNOT OPEN
  CONFIG FILE` depending on what had been sent before (found in
  UBoot64-v2 #22). `""` still sends no name (firmware default file).

### Documented

- `uii_load_config()` tested on hardware (Ultimate 64-II, Ultimate 64
  Elite, firmware 3.15a). Firmware 3.15a can't open a settings file in
  the root of a storage device with this command; loading re-applies
  every settings store with pending changes, which can restart a drive
  (a mount right after can answer `90,DRIVE NOT PRESENT` for a moment).
- `uii_get_deviceinfo()` tested (through `uii_parse_deviceinfo()`, #2).

## [1.2.0] - 2026-10-02

### Added

- `uii_turbo_probe_max()`: classifies the maximum turbo speed as 48 or
  64 MHz by timing a 64,764-cycle loop against the VIC raster counter
  (based on `upic_select_display_path()` by Christian Gleissner,
  mandelbrot-upic). Handles the forced 1 MHz window the Ultimate 64
  applies for about 2 s after every reset: a result counts only when two
  consecutive loops agree. Also a better "turbo engaged" check than
  `uii_turbo_detect()`. `uii_turbo_probe_lines()` returns one raw
  measurement. Tested on an Ultimate 64-II (16 lines at 64 MHz, 21 at
  index 14 = 48 MHz); not yet on a real 48 MHz Ultimate 64 / Elite I.
- `tests/speed_probe.c` (`make speedprobe`): hardware test of the probe.

### Fixed

- `uii_sendcommand()` waits (bounded) for a pending abort to finish before
  pushing. `uii_detect()` writes ABORT on every call, so the first command
  after `uii_wait_for_uci()` could be answered after the handshake was
  reset and read back empty: `uii_identify()` failed in 2 of 6 starts in
  UltimateDemo2026, 0 of 22 after the fix.

### Documentation

- `TURBOCONTROL_MANUAL.md`: the raster counter runs at real time on the
  U64 (the manual said it was CPU-clocked), the forced 1 MHz window, the
  probe, and a revised explanation of the 2026-09-23 "transition bug".

## [1.1.0] - 2026-10-02

Ultimate 64 hardware modules moved in from UltimateDemo2026, renamed to the
library's conventions (file `ultimate_<module>_lib`, functions
`uii_<module>_*`). No change to the 1.0.0 UCI API.

### Added

- `ultimate_turbo_lib`: U64 turbo speed control (`$D031`) and
  `uii_turbo_detect()` (CIA TOD timing, confirms turbo is engaged; 48 vs
  64 MHz comes from `uii_get_hwinfo()`). Corrected `TURBO_SPEED_*` table.
  Manual: `docs/TURBOCONTROL_MANUAL.md`.
- `ultimate_audio_lib`: Ultimate Audio 7-voice DMA layer at `$DF20`, plus
  `uii_audio_reu_fetch()` (REU to C64 RAM).
- `ultimate_modplay_lib`: ProTracker MOD player (REU samples, CIA1 timer A
  IRQ, zero page `$03-$51` saved in the IRQ wrapper). Manual for both:
  `docs/ULTIMATEAUDIO_MANUAL.md`.
- Hardware test status for these 30 functions in `docs/UCILIB_MANUAL.md`
  section 20; 11 untested, marked `[UNTESTED]`.
- `make check` also compiles the MOD player (`tests/compile_modplay.c`).

### Renamed (compared with the UltimateDemo2026 copies)

`turbo_*` → `uii_turbo_*`, `benchmark_delay` → `uii_turbo_benchmark_delay`,
`audio_*` → `uii_audio_*`, `reu_fetch` → `uii_audio_reu_fetch`,
`modplay_*` → `uii_modplay_*`, the player state `modplay` → `uii_modplay`.
Headers `turbo.h`, `audio.h`, `modplay.h` → `ultimate_turbo_lib.h`,
`ultimate_audio_lib.h`, `ultimate_modplay_lib.h`. Constants keep their
names (`TURBO_*`, `AUDIO_*`, `MOD_*`, ...).

### Notes

- These modules target the C64 on an Ultimate 64 (turbo registers,
  Ultimate Audio). The MOD player chains to the C64 KERNAL IRQ (`$EA31`)
  and does not run on a C128.
- Taking the address of the audio functions the MOD player's tick calls
  makes Oscar64 reject the tick ("Function too complex for interrupt");
  call them directly.

## [1.0.0] - 2026-10-02

First release as a separate library. Previously every project carried its
own copy; this release is the DMBoot 128 v5 copy (itself built on the
UBoot64-v2 copy) plus the start-up hang fix from mandelbrot-upic.

### Included

- `ultimate_common_lib`: registers, protocol engine, detection, firmware
  3.15+ unlock, `uii_wait_for_uci()`, partitions, palette, hardware info.
- `ultimate_dos_lib`: file and directory I/O, REU transfer, drives, control
  commands, storage media helpers.
- `ultimate_time_lib`, `ultimate_network_lib` (TCP/UDP client sockets),
  `ultimate_softiec_lib` (firmware 3.15+).
- `ultimate_http_lib` (new): all 22 commands of the firmware 3.15 HTTP
  target. **Not tested on real hardware yet.**
- Complete firmware 3.15a command coverage: also `uii_save_c64_memory()`
  (control `0x0F`, U64 only; untested) and `uii_del_partition()` (SoftIEC
  `0x21`; answers OK but did not remove the partition on hardware).
  `uii_getipaddress()` is now declared in the header.
- Hardware test status per function: manual section 20, and `[UNTESTED]`
  on the prototypes of the 66 functions no project has used on hardware.
- `DATA_QUEUE_SZ`, `STATUS_QUEUE_SZ` and `UII_COMMAND_MAX` can be set from
  the build (`-dDATA_QUEUE_SZ=896`).
- No dynamic allocation: one shared command buffer (`uii_command_buffer()`).
- Version macros `UII_LIB_VERSION_MAJOR/MINOR/PATCH` and `UII_LIB_VERSION`.

### Fixed

- Start-up hang in the command handshake (found and fixed by Christian
  Gleissner in mandelbrot-upic, commit 6379683): the firmware 3.15 unlock is
  sent only when the UCI isn't mapped, the write-only control register is
  assigned instead of OR-ed, ERROR is status bit 3, `uii_sendcommand()`
  waits until the command is taken, and a reply left over from an earlier
  command is released with DATA_ACC.

### Changed compared with older project copies

- `uii_load_reu(addr, len)` / `uii_save_reu(addr, len)` are
  `uii_load_reu_at()` / `uii_save_reu_at()`; `uii_load_reu(size)` /
  `uii_save_reu(size)` handle a whole REU image (upstream naming).
- `uii_find_media_path()` takes the result buffer size.
- Removed: the TCP listener functions (the firmware has no listener
  commands).
