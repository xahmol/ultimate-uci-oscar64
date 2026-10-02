# Changelog

All notable changes to ultimate-uci-oscar64. Versions follow
[semantic versioning](https://semver.org/): MAJOR for incompatible API
changes (renamed or removed functions, changed parameters), MINOR for new
functions, PATCH for fixes that keep the API.

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
