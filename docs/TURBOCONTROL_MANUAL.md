# Turbo Control Library Manual

**Ultimate 64 CPU Speed Control for Oscar64**

Library files: `include/ultimate_turbo_lib.h` / `include/ultimate_turbo_lib.c`

---

## Contents

1. [Overview](#1-overview)
2. [Hardware Background](#2-hardware-background)
3. [Firmware Prerequisites](#3-firmware-prerequisites)
4. [Register Reference](#4-register-reference)
5. [Constants and Defines](#5-constants-and-defines)
6. [Function Reference](#6-function-reference)
7. [Detection Method](#7-detection-method)
8. [Typical Usage Patterns](#8-typical-usage-patterns)
9. [Limitations and Known Issues](#9-limitations-and-known-issues)

---

## 1. Overview

The turbo control library provides a simple API to:

- **Detect** whether the U64 turbo speed registers are active, and **classify** the maximum speed as 48 or 64 MHz with a raster-timed probe (since 1.2.0).
- **Set** any of the 16 speed steps from 1 MHz to the hardware maximum.
- **Get** the current speed register value.

The primary control register is `$D031`. Two measurement-based methods: `uii_turbo_detect()` times a loop against CIA1 TOD (Time Of Day) and confirms that turbo is engaged; `uii_turbo_probe_max()` times a short loop against the VIC raster counter, which advances once per real PAL line at any CPU speed, and tells 48 from 64 MHz. See §7.

---

## 2. Hardware Background

### Ultimate 64 CPU speed

The Ultimate 64 (U64) FPGA replaces the C64's 6510 CPU with a re-implementation that can run at higher clock multiples. The actual maximum depends on the hardware revision:

| Hardware | Maximum CPU speed |
|----------|------------------|
| Ultimate 64 (original) | ~48 MHz |
| Ultimate 64 Elite I | ~48 MHz |
| Ultimate 64 Elite II | ~64 MHz |
| C64U | ~64 MHz |

The FPGA presents a standard 6510-compatible 8-bit CPU to software; timing-sensitive code (SID register timing, raster IRQs, etc.) must be rewritten for turbo speeds.

### Which clocks track real time on U64

The VIC-II raster counter (`$D012`) advances once per real video line
(64 µs on PAL) at any CPU speed: the VIC keeps its normal timing and only
the CPU gets more cycles per phi2 cycle (63 at index 15 on an Elite II /
C64U, 47 on a U64 / Elite I; the VIC takes one sub-slot). That makes it a
fine real-time reference with about one line (64 µs) of resolution,
which `uii_turbo_probe_max()` uses. CIA1 TOD advances at the mains rate
in tenths of a second and is used by `uii_turbo_detect()`.

Before 2026-10-02 this manual stated that the raster counter is clocked
at the CPU frequency on U64. That was wrong, and it ruled out the best
available clock for years; the probe's results (§7) show the raster
counter at real time. Whether the CIA timers are CPU-clocked on U64 has
not been verified either way.

**Forced 1 MHz window:** the Ultimate 64 runs the CPU at 1 MHz for about
2 seconds after every CPU reset, whatever `$D031` says (measured
2026-10-02 on an Ultimate 64-II, firmware 3.15a, program started over
REST; the window also follows IEC bus activity briefly). Starting a
program from the Ultimate menu or REST includes a reset, so any speed
measurement at program start runs inside that window. Both detection
methods have to allow for it.

### Badlines

At 1 MHz the VIC-II "steals" the CPU bus for ~40 cycles every 8 raster lines (badlines). Setting bit 7 of `$D031` suppresses badline CPU stalls. Strongly recommended when running at turbo speed. The VIC-II continues to render normally; only the CPU stall is removed.

---

## 3. Firmware Prerequisites

The U64 firmware exposes turbo control through a **menu setting**:

**Menu path:** `F2` → `C64 and cartridge settings` → `Turbo Mode`

| Setting | Behaviour |
|---------|-----------|
| `Off` | Turbo disabled; `$D031` reads `$FF` |
| `Turbo Enable Bit` | `$D030` bit 0 enables turbo at the firmware-configured speed |
| `U64 Turbo Registers` | `$D031` bits 0–3 directly set the speed index |

**Recommended: "U64 Turbo Registers" mode.**

In "Turbo Enable Bit" mode, `$D031` reads as `0x00` and the speed is controlled by `$D030` bit 0 + the firmware CPU Speed menu. `uii_turbo_detect()` handles this case by checking `$D030` bit 0 as a fallback when `$D031 == 0x00`.

---

## 4. Register Reference

### `$D031` — U64 Turbo Control (read/write)

| Bits | Name | Description |
|------|------|-------------|
| 3–0 | Speed index | 0 = 1 MHz, 1–14 = intermediate speeds, 15 = maximum |
| 6–4 | Reserved | Write 0 |
| 7 | Badline mask | 0 = normal VIC-II badline CPU stalls; 1 = suppress |

**Detection:** Reading `$D031` returns `$FF` when turbo registers are unavailable. Any non-`$FF` value confirms U64 turbo registers are present.

**Speed index frequencies** (from 1541ultimate `software/u64/u64_config.cc`, `speeds_u64` / `speeds_u64ii`):

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| U64 / Elite I (MHz) | 1 | 2 | 3 | 4 | 5 | 6 | 8 | 10 | 12 | 14 | 16 | 20 | 24 | 32 | 40 | 48 |
| Elite II / C64U (MHz) | 1 | 2 | 3 | 4 | 6 | 8 | 10 | 12 | 14 | 16 | 20 | 24 | 32 | 40 | 48 | 64 |

From index 4 upward the U64 / Elite I table is one step behind the Elite II / C64U table. Before 2026-10-02 this manual and `ultimate_turbo_lib.h` named indexes 6–13 after neither table (12, 16, 20, 24, 28, 32, 36, 40 MHz).

Index 15 is the hardware maximum. On Elite-I this is ~48 MHz; on Elite-II / C64U it is ~64 MHz. Software cannot distinguish these cases from the register value alone — both report index 15.

### `$D030` — Turbo Enable Bit (conditional)

| Bit | Description |
|-----|-------------|
| 0 | In "Turbo Enable Bit" firmware mode: 1 = enable turbo at firmware-configured speed |

In "U64 Turbo Registers" mode, `$D030` reads as `$FF` (open bus) and bit 0 is always 1.

---

## 5. Constants and Defines

All constants are in `include/ultimate_turbo_lib.h`.

### Detection result constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `TURBO_NOT_PRESENT` | 0 | Turbo not genuinely engaged ($D031==$FF, or the CIA-TOD benchmark shows no real speedup) |
| `TURBO_DETECTED` | 1 | Turbo genuinely engaged and accelerating the CPU — confirms *that*, not *how fast*; see §9 |

Results of `uii_turbo_probe_max()` (1.2.0):

| Constant | Value | Meaning |
|----------|-------|---------|
| `TURBO_MAX_UNKNOWN` | 0 | No stable measurement: turbo not engaged, no turbo registers, or gave up after 256 loops |
| `TURBO_MAX_48MHZ` | 1 | Maximum speed 48 MHz: Ultimate 64 / Elite I |
| `TURBO_MAX_64MHZ` | 2 | Maximum speed 64 MHz: Ultimate 64 Elite II / C64 Ultimate |

Probe thresholds: `TURBO_PROBE_START` ($20, start line), `TURBO_PROBE_64MHZ`
(19: fewer lines is 64 MHz), `TURBO_PROBE_VALID` (26: fewer lines is 48 MHz).

### Speed index constants

The names follow the Elite II / C64U column.

| Constant | `$D031` bits 0–3 | Elite II / C64U | U64 / Elite I |
|----------|-----------------|-----------------|---------------|
| `TURBO_SPEED_1MHZ` | `0x00` | 1 MHz | 1 MHz |
| `TURBO_SPEED_2MHZ` | `0x01` | 2 MHz | 2 MHz |
| `TURBO_SPEED_3MHZ` | `0x02` | 3 MHz | 3 MHz |
| `TURBO_SPEED_4MHZ` | `0x03` | 4 MHz | 4 MHz |
| `TURBO_SPEED_6MHZ` | `0x04` | 6 MHz | 5 MHz |
| `TURBO_SPEED_8MHZ` | `0x05` | 8 MHz | 6 MHz |
| `TURBO_SPEED_10MHZ` | `0x06` | 10 MHz | 8 MHz |
| `TURBO_SPEED_12MHZ` | `0x07` | 12 MHz | 10 MHz |
| `TURBO_SPEED_14MHZ` | `0x08` | 14 MHz | 12 MHz |
| `TURBO_SPEED_16MHZ` | `0x09` | 16 MHz | 14 MHz |
| `TURBO_SPEED_20MHZ` | `0x0A` | 20 MHz | 16 MHz |
| `TURBO_SPEED_24MHZ` | `0x0B` | 24 MHz | 20 MHz |
| `TURBO_SPEED_32MHZ` | `0x0C` | 32 MHz | 24 MHz |
| `TURBO_SPEED_40MHZ` | `0x0D` | 40 MHz | 32 MHz |
| `TURBO_SPEED_48MHZ` | `0x0E` | 48 MHz | 40 MHz |
| `TURBO_SPEED_MAX` | `0x0F` | 64 MHz | 48 MHz |

### Badline and convenience constants

| Constant | Value | Effect |
|----------|-------|--------|
| `TURBO_BADLINES_ON` | `0x00` | Normal badline stalls |
| `TURBO_BADLINES_OFF` | `0x80` | Suppress badline stalls |
| `TURBO_FULL` | `0x8F` | `TURBO_SPEED_MAX \| TURBO_BADLINES_OFF` |

### Detection calibration constants

| Constant | Value | Description |
|----------|-------|-------------|
| `COUNT` | `0xFFFF` | 16-bit down-counter value passed to `uii_turbo_benchmark_delay()` |
| `THRESHOLD_DETECT` | 6 | Elapsed tenths at or above which the CPU is classified as 1 MHz (no turbo) |

2026-09-20: replaced the deliberately-unoptimised C loop (`ITERS=300`, 300 × 200 = 60,000 nop-loop
passes) with a hand-written 6502 assembly loop (see §7 and `ultimate_turbo_lib.c`) — guidance from Bart van
Leeuwen (private correspondence, 2026-09-20) was that a short, fixed-cycle-cost assembly loop has
far less timing jitter than compiler-generated `-O0` code, needing far less safety margin around
`THRESHOLD_DETECT`. `COUNT=0xFFFF` (the full 16-bit range, one pass) was originally a **computed**
estimate from the loop's known, cycle-accurate cost (~20.016 cycles/count):

- ~1,311,738 cycles/pass → ~13.1 tenths at 1 MHz (no turbo)
- ~0.27 tenths at 48 MHz, ~0.21 tenths at 64 MHz (turbo engaged)

2026-09-23: empirically re-measured on real Ultimate 64-II hardware — 1 MHz baseline read 12 tenths
(close to the ~13.1 estimate), turbo-engaged read 0 (well under threshold). `THRESHOLD_DETECT=6`
sits roughly halfway, well clear of both sides — confirmed correct as-is. The same session found
and fixed a real, separate bug in `uii_turbo_detect()` itself: an unclean speed transition when an
auto-loaded `.cfg` leaves `$D031` pre-set to a non-zero speed index (see the fix note under
`uii_turbo_detect` below) — a transition-settling bug, not a miscalibration of these constants.

If `uii_turbo_detect()` misclassifies your hardware, call `uii_turbo_benchmark_delay(COUNT)` directly and print
the return value (with and without turbo enabled) to re-derive `THRESHOLD_DETECT` empirically.

---

## 6. Function Reference

### `uii_turbo_benchmark_delay`

```c
int uii_turbo_benchmark_delay(unsigned int count);
```

Run a short, hand-written 6502 assembly counting loop and return elapsed time in CIA1 TOD tenths of a second. Used internally by `uii_turbo_detect()`.

The loop is a standard borrow-based 16-bit decrement-to-zero counter (`#pragma optimize(0)`, `__noinline`, `volatile unsigned char` hi/lo bytes — see `ultimate_turbo_lib.c` for why `volatile` is required here, not just habit). Each count costs a fixed, precisely computable number of cycles (~20.016 average), giving CIA TOD enough real time to advance with far less run-to-run jitter than a compiler-generated loop.

Resets CIA1 TOD to `00:00.0` on entry and reads it on exit. Wraps with SEI/CLI. `count` is destroyed by the loop.

**Direct use:** call `uii_turbo_benchmark_delay(COUNT)` and print the return value to calibrate `THRESHOLD_DETECT` for your hardware.

---

### `uii_turbo_detect`

```c
char uii_turbo_detect(void);
```

Confirm turbo is genuinely engaged via CIA TOD timing. Forces a clean 1 MHz baseline first (its own
settle pass), then sets CPU to maximum speed and calls `uii_turbo_benchmark_delay(COUNT)` twice more — once
as a throwaway pass to let the FPGA clock-domain change settle, once to measure. The elapsed tenths
are compared against a single threshold. Does **not** classify the MHz ceiling — see §9 for why,
and how to get that from `CTRL_CMD_GET_HWINFO` instead.

**Returns:** `TURBO_NOT_PRESENT` or `TURBO_DETECTED`.

| Result | Condition |
|--------|-----------|
| `TURBO_DETECTED` | elapsed < `THRESHOLD_DETECT` (genuinely accelerated) |
| `TURBO_NOT_PRESENT` | elapsed ≥ `THRESHOLD_DETECT` (6 tenths — running at ~1 MHz) |

**2026-09-23 fix, confirmed on real Ultimate 64-II hardware via c64bridge:** an auto-loaded `.cfg`'s
own Turbo Control setting can leave `$D031` already at a non-zero speed index before this function
ever runs. Jumping directly from that pre-existing state to max speed (the original implementation)
settled unreliably — intermittent false `TURBO_NOT_PRESENT` roughly half the time, reproducible
across repeated soft resets of the identical binary. Forcing a genuine 1 MHz baseline first (a real
`$D030` enable-bit transition, with its own settle pass) before jumping to max fixed it: confirmed
reliable across multiple consecutive real-hardware runs afterward.

Restores `$D031` to 1 MHz after measuring. Call once at startup; worst case (no turbo present)
takes ~3.9s at 1 MHz across the three `uii_turbo_benchmark_delay()` passes now used, ~0.05s if turbo is
genuinely engaged. See §7 for full method description and threshold calibration.

---

### `uii_turbo_set`

```c
void uii_turbo_set(char control);
```

Write a control byte to `$D031` and enable via `$D030`.

```c
uii_turbo_set(TURBO_FULL);                           // max speed, no badlines
uii_turbo_set(TURBO_SPEED_24MHZ | TURBO_BADLINES_ON);// 24 MHz (20 on U64/Elite I), keep badlines
uii_turbo_set(TURBO_SPEED_1MHZ);                     // 1 MHz
```

Speed change takes effect on the very next CPU cycle.

---

### `uii_turbo_fast`

```c
void uii_turbo_fast(void);
```

Shorthand: `uii_turbo_set(TURBO_SPEED_MAX | TURBO_BADLINES_OFF)`.

---

### `uii_turbo_slow`

```c
void uii_turbo_slow(void);
```

Shorthand: `uii_turbo_set(TURBO_SPEED_1MHZ)`.

---

### `uii_turbo_get`

```c
unsigned char uii_turbo_get(void);
```

Read the current `$D031` value. Returns `0xFF` if registers unavailable.

---

### `uii_turbo_probe_lines` *new in 1.2.0*

```c
unsigned char uii_turbo_probe_lines(char control);
```

One raster-timed measurement: sets `$D031` to `control`, waits for raster
line `TURBO_PROBE_START` ($20), runs a 64,764-cycle loop with interrupts
disabled and returns the raster lines it took, modulo 256. Measured on an
Ultimate 64-II: 16 lines at index 15 (64 MHz), 21 at index 14 (48 MHz),
92 for a loop entirely at 1 MHz. Leaves `$D031` at `control`. PAL timing.
For diagnostics and tests; use `uii_turbo_probe_max()` to classify.

---

### `uii_turbo_probe_max` *new in 1.2.0*

```c
char uii_turbo_probe_max(void);
```

**Returns:** `TURBO_MAX_64MHZ` (Elite II / C64 Ultimate), `TURBO_MAX_48MHZ`
(Ultimate 64 / Elite I) or `TURBO_MAX_UNKNOWN` (no stable result: turbo
not engaged, or no turbo registers).

Measures at `TURBO_FULL` until two consecutive loops fall in the same
class (fewer than 19 lines: 64 MHz; 19-25: 48 MHz; anything else, such as
a loop at 1 MHz, is measured again). The forced 1 MHz window after a
reset can end during at most one loop, so it can't produce a wrong
result, only a delay. Every loop rewrites `$D031`. Gives up after 256
loops (about 20 s if turbo stays off). Restores the previous `$D030`/`$D031`
values. Takes about 0.1 s with turbo running, up to about 2 s inside the
forced window.

Also a better "turbo engaged" check than `uii_turbo_detect()`: a result
other than `TURBO_MAX_UNKNOWN` means the CPU really ran at turbo speed.

Based on `upic_select_display_path()` by Christian Gleissner in
mandelbrot-upic (`include/upic_viewer.c`, PR #2).

---

## 7. Detection Method

### Overview

`uii_turbo_detect()` first forces a clean 1 MHz baseline (its own settle pass, fixing a real transition
bug — see below), then sets the CPU to maximum speed and runs `uii_turbo_benchmark_delay(COUNT)` twice more —
once as a throwaway pass purely to let the FPGA clock-domain change settle, once to measure. The
result is compared against a single empirical threshold — confirming turbo is genuinely engaged, not
classifying how fast:

| Result | Condition |
|--------|-----------|
| `TURBO_DETECTED` | elapsed < `THRESHOLD_DETECT` (~0.2–0.3 tenths) |
| `TURBO_NOT_PRESENT` | elapsed ≥ `THRESHOLD_DETECT` (~13.1 tenths at 1 MHz) |

**"Transition bug", fixed 2026-09-23 — explanation revised 2026-10-02:**
the original implementation measured right after switching to max speed
and gave a false `TURBO_NOT_PRESENT` roughly half the time on an Ultimate
64-II. The fix forces 1 MHz first and runs a settle pass (about 1.3 s at
1 MHz) before switching up. The diagnosis then was an unreliable speed
transition. With the forced 1 MHz window now measured (about 2 s after a
reset, §2), the more likely cause is that the measurement overlapped the
end of that window, and the settle pass works mainly by spending time
until the window is over. That margin is not guaranteed; use
`uii_turbo_probe_max()`, which handles the window explicitly.

For the MHz ceiling (48 vs 64) use `uii_turbo_probe_max()` (see below).
The TOD measurement can't resolve it: one pass at 64 and at 48 MHz both
read 0 tenths (about 21 and 28 ms), and longer multi-second passes tried
on 2026-09-14/23 gave inconsistent results — most likely because they
overlapped the forced 1 MHz window.

### The raster-timed probe

`uii_turbo_probe_lines()` waits for raster line $20 and runs a loop of
exactly 64,764 cycles (36 outer passes of 1799; every loop-back an
absolute `jmp`, no I/O access inside the loop, interrupts off), then
reads `$D012`:

| CPU | Expected lines | Measured (Ultimate 64-II, 3.15a) |
|---|---|---|
| 64 MHz (63 cycles per phi2) | 64764 / (63 × 63) = 16.3 | 16 |
| 48 MHz (47 cycles per phi2) | 64764 / (63 × 47) = 21.9 | 21 (index 14) |
| 1 MHz | 1028 (ends 92 lines after the start line, mod 256) | 92 |

A loop entirely at 1 MHz always ends 92 lines (mod 256) after the start
line, which is rejected. Occasional 18-line results at 64 MHz were seen
while a PC read C64 memory over the REST API (DMA pauses the CPU); still
below the 19-line threshold. `tests/speed_probe.c` (`make speedprobe`)
logs these values on hardware, including the first 12 s after start.
Not yet run on a real 48 MHz Ultimate 64 / Elite I or on NTSC.

### Why CIA TOD timing does work

CIA1 TOD (Time Of Day) advances at the real 50/60 Hz mains rate.  The key is that the measured loop must run long enough in *real time* for TOD tenths to accumulate.

`uii_turbo_benchmark_delay()` (2026-09-20) is a short, hand-written 6502 assembly loop — a standard borrow-based 16-bit decrement-to-zero counter — not compiler-generated code. Per guidance from Bart van Leeuwen (private correspondence, 2026-09-20): a short, tight, hand-written loop with a fixed, precisely computable cycle cost per iteration has far less run-to-run timing jitter than even deliberately-unoptimised `-O0` C, so far less safety margin is needed around `THRESHOLD_DETECT`. Each count costs ~20.016 cycles on average (confirmed against the compiled `.asm`); at `COUNT=0xFFFF` that's ~1,311,738 cycles per pass. At turbo speed the loop completes in a fraction of a tenth; at 1 MHz it takes ~1.3 s. CIA TOD therefore advances measurably during the loop at any speed.

An earlier version of this loop (pre-2026-09-20) deliberately used unoptimised C (`#pragma optimize(0)`, `volatile int` counters, inline `nop`) to burn cycles instead — functionally similar in spirit, but with compiler-dependent, less predictable per-iteration cost.

### Threshold calibration

The defaults (`COUNT=0xFFFF`, `THRESHOLD_DETECT=6`) were originally a **computed** estimate from the loop's known cycle cost, confirmed by real-hardware measurement 2026-09-23 (1 MHz baseline read 12 tenths, turbo-engaged read 0) — see §5. If `uii_turbo_detect()` misclassifies your hardware, call `uii_turbo_benchmark_delay(COUNT)` directly (with and without turbo enabled) and print the return value, then adjust `THRESHOLD_DETECT` to bracket the observed values.

---

## 8. Typical Usage Patterns

### Basic detect-and-run

```c
#include "ultimate_turbo_lib.h"

void main(void) {
    char cls = uii_turbo_detect();
    if (cls != TURBO_NOT_PRESENT) {
        uii_turbo_fast();
        run_demo();
        uii_turbo_slow();
    } else {
        show_error_no_turbo();
    }
}
```

### Speed-adaptive code paths

`uii_turbo_detect()` only tells you *whether* turbo is engaged, not *how fast* — for that, classify from `CTRL_CMD_GET_HWINFO`'s product-name string instead (a hardware-identity fact, not a measurement):

```c
char cls = uii_turbo_detect();
if (cls == TURBO_NOT_PRESENT) {
    // 1 MHz fallback
} else {
    // Turbo engaged. For the MHz ceiling, classify hwinfo's device-type
    // string ("Ultimate 64-II" = 64 MHz-capable; "Ultimate 64 Elite" =
    // 48 MHz-capable) at the application level -- see src/main.c in
    // UltimateDemo2026 for a worked example, including the C64U caveat:
    // C64U reports plain "Ultimate 64", identical to the original
    // non-Elite U64's string, so that specific ambiguous string should
    // default toward the newer/faster tier (C64U is both the more common
    // case today and documented as 64 MHz-capable) rather than guessing 48.
}
```

### Selective turbo (computation fast, I/O at 1 MHz)

```c
void update_frame(void) {
    uii_turbo_fast();
    compute_effects();

    uii_turbo_slow();          // drop before timing-sensitive I/O
    update_sid();

    uii_turbo_fast();
    render_screen();
    uii_turbo_slow();
}
```

### Speed label from $D031 index (careful: not after uii_turbo_detect())

```c
uii_turbo_set(TURBO_SPEED_24MHZ | TURBO_BADLINES_ON);
unsigned char idx = uii_turbo_get() & 0x0F;   // 0x0B -- reads back what you just set
```

`uii_turbo_get()` reads whatever `$D031` currently holds, so this only reflects a speed *you* set. It does **not** work after `uii_turbo_detect()`: that function restores `$D031` to `TURBO_SPEED_1MHZ` before returning (see §7), so `uii_turbo_get()` immediately afterward always reads index `0`, regardless of what turbo speed was actually detected. Mixing the two was a real, confirmed bug in an earlier version of this pattern — see §9.

---

## 9. Limitations and Known Issues

### Firmware setting dependency

- **Off**: `$D031 == $FF` → `TURBO_NOT_PRESENT`.
- **Turbo Enable Bit**: `$D031 == 0x00`; detection uses `$D030` bit 0 fallback.
- **U64 Turbo Registers**: full support, recommended.

### 48 vs 64 MHz

Classify with `uii_turbo_probe_max()` (since 1.2.0). Before that, the
library left this to the application, which used
`CTRL_CMD_GET_HWINFO`'s product-name string. That string is ambiguous:
a C64 Ultimate and an original (48 MHz) Ultimate 64 both report plain
`"Ultimate 64"` (confirmed on a C64U, 2026-09-20), so the original U64
was labelled 64 MHz. Keep `uii_get_hwinfo()` for showing the product
name, not for the speed.

The probe assumes PAL timing; on NTSC it still classifies 48/64 MHz
correctly in theory (15.8 and 21.2 lines) but has not been tested.

### Speed changes are instantaneous

`uii_turbo_set()` takes effect on the very next cycle. Arrange timing-critical code so `uii_turbo_slow()` is called *before* entering the critical section.

### Non-U64 hardware

On standard C64/C128/SuperCPU, `$D031 == $FF` → `TURBO_NOT_PRESENT` immediately.
