/*****************************************************************
Ultimate 64 Turbo Control Library
ultimate-uci-oscar64 -- https://github.com/xahmol/ultimate-uci-oscar64

Targets the U64-specific $D031 turbo speed register.
Firmware menu must have "Turbo Mode" set to "U64 Turbo Registers"
for detection to work correctly.

Detection method — CIA TOD timing against a hand-written assembly loop:
  uii_turbo_detect() calls uii_turbo_benchmark_delay() which uses CIA1 TOD
  (Time Of Day) as a real-time reference.  The measured function is a
  short, hand-written 6502 assembly loop (a standard borrow-based
  16-bit decrement-to-zero counter, #pragma optimize(0), __noinline)
  with a fixed, precisely computable cycle cost per count -- not a
  deliberately-unoptimised C loop.  Per guidance from Bart van
  Leeuwen (private correspondence, 2026-09-20): a short, tight,
  hand-written loop has far less timing jitter than compiler-
  generated code, even at -O0, so far less safety margin is needed
  around THRESHOLD_DETECT.  uii_turbo_detect() also runs one throwaway
  uii_turbo_benchmark_delay() pass immediately after setting the speed register,
  purely to let the FPGA clock-domain change settle before the real
  measurement starts.  The measured result (tenths of a second) is
  compared against a single threshold to confirm turbo is genuinely
  engaged.

48 vs 64 MHz: use uii_turbo_probe_max() (library 1.2.0), which times a
short loop against the VIC raster counter. The raster counter advances
once per real PAL line at any CPU speed -- an earlier note here said it
is CPU-clocked on U64, which was wrong. CIA TOD (0.1 s steps) can't
resolve 48 vs 64 MHz; attempts on 2026-09-14/23 were inconsistent,
most likely because they overlapped the forced 1 MHz window the U64
applies for about 2 s after every reset. CTRL_CMD_GET_HWINFO's product
string can't either: a C64 Ultimate and an original 48 MHz Ultimate 64
both report plain "Ultimate 64". See docs/TURBOCONTROL_MANUAL.md
sections 2, 7 and 9.

Supported hardware:
  Ultimate 64 original / Elite I  — max ~48 MHz
  Ultimate 64 Elite II / C64U     — max ~64 MHz
******************************************************************/

#ifndef _ULTIMATE_TURBO_LIB_H_
#define _ULTIMATE_TURBO_LIB_H_

// ---------------------------------------------------------------
// Detection result constants
// ---------------------------------------------------------------

#define TURBO_NOT_PRESENT  0
// $D031 reads $FF (no U64, or turbo registers not enabled in
// firmware), OR the CIA-TOD benchmark shows no real speedup
// (turbo not genuinely engaged, even if the register write "took" --
// e.g. firmware's own Turbo Mode menu setting isn't honoring it).

#define TURBO_DETECTED     1
// Turbo is genuinely engaged and accelerating the CPU. This only
// confirms THAT turbo is active, not HOW FAST -- see the file header
// above for why, and how to get the MHz ceiling instead.

// ---------------------------------------------------------------
// $D031 control byte composition
//
//   bits 0-3 : speed index  (0 = 1 MHz, 15 = max)
//   bit  7   : badline mask (0 = normal stalls, 1 = suppress)
//   bits 4-6 : reserved, write 0
// ---------------------------------------------------------------

// Speed table per hardware (MHz by index 0x0-0xF), from the firmware:
//
//   U64/Elite I   1 2 3 4 5 6 8  10 12 14 16 20 24 32 40 48
//   Elite II/C64U 1 2 3 4 6 8 10 12 14 16 20 24 32 40 48 64
//
// The names below follow the Elite II / C64U column. Corrected
// 2026-10-02 (issue #3): indexes 6-13 previously carried names (12, 16,
// 20, 24, 28, 32, 36, 40 MHz) that match neither table. Correction by
// Christian Gleissner in mandelbrot-upic (PR #2, include/ultimate_turbo_lib.h).
#define TURBO_SPEED_1MHZ    0x00   // Standard 1 MHz (both)
#define TURBO_SPEED_2MHZ    0x01
#define TURBO_SPEED_3MHZ    0x02
#define TURBO_SPEED_4MHZ    0x03
#define TURBO_SPEED_6MHZ    0x04   // 5 MHz on U64 / Elite I
#define TURBO_SPEED_8MHZ    0x05   // 6 MHz on U64 / Elite I
#define TURBO_SPEED_10MHZ   0x06   // 8 MHz on U64 / Elite I
#define TURBO_SPEED_12MHZ   0x07   // 10 MHz on U64 / Elite I
#define TURBO_SPEED_14MHZ   0x08   // 12 MHz on U64 / Elite I
#define TURBO_SPEED_16MHZ   0x09   // 14 MHz on U64 / Elite I
#define TURBO_SPEED_20MHZ   0x0A   // 16 MHz on U64 / Elite I
#define TURBO_SPEED_24MHZ   0x0B   // 20 MHz on U64 / Elite I
#define TURBO_SPEED_32MHZ   0x0C   // 24 MHz on U64 / Elite I
#define TURBO_SPEED_40MHZ   0x0D   // 32 MHz on U64 / Elite I
#define TURBO_SPEED_48MHZ   0x0E   // 40 MHz on U64 / Elite I
#define TURBO_SPEED_MAX     0x0F   // 64 MHz Elite II / C64U, 48 MHz U64 / Elite I

#define TURBO_BADLINES_ON   0x00   // Normal VIC-II badline CPU stalls
#define TURBO_BADLINES_OFF  0x80   // Suppress badline stalls

// Convenience: full-speed control byte
#define TURBO_FULL  (TURBO_SPEED_MAX | TURBO_BADLINES_OFF)

// ---------------------------------------------------------------
// uii_turbo_benchmark_delay() calibration constants
//
// COUNT: 16-bit down-counter value passed to uii_turbo_benchmark_delay().
// THRESHOLD_DETECT: elapsed tenths of a second at or above which
//   the CPU is running at ~1 MHz (no turbo or turbo disabled).
//   Below this, turbo is genuinely engaged (TURBO_DETECTED),
//   regardless of which MHz tier the hardware actually reaches.
//
// 2026-09-20: replaced the deliberately-unoptimised C loop (ITERS=300,
// outer*inner=60000 nop-loop passes) with a hand-written assembly
// borrow-based 16-bit decrement loop (see ultimate_turbo_lib.c) -- Bart van Leeuwen's
// guidance was that a short, fixed-cycle-cost assembly loop has far less
// timing jitter than compiler-generated -O0 code, needing far less safety
// margin. COUNT=0xFFFF (max 16-bit range, one pass) was originally a
// COMPUTED estimate from the loop's known cycle cost (~20.016
// cycles/count, see ultimate_turbo_lib.c):
//   ~1,310,700 cycles/pass -> ~13.1 tenths at 1 MHz (no turbo)
//                           -> ~0.27 tenths at 48 MHz, ~0.20 tenths at 64 MHz
//
// 2026-09-23: empirically re-measured on real Ultimate 64-II hardware via
// c64bridge (src/test_turbocal.c probe), confirming the estimate's order
// of magnitude and THRESHOLD_DETECT=6's placement:
//   1 MHz baseline:  elapsed = 12 (close to the ~13.1 estimate)
//   64 MHz (MAX):    elapsed = 0  (well under threshold, correctly DETECTED)
// This same session also found and fixed a real false-negative in
// uii_turbo_detect() itself (an unclean speed transition when an auto-loaded
// .cfg leaves $D031 pre-set to a non-zero index -- see ultimate_turbo_lib.c's own
// comment on uii_turbo_detect()); the false-negative was a transition-
// settling bug, not a THRESHOLD_DETECT/COUNT miscalibration -- these
// constants were already correct once the transition itself was fixed.
// THRESHOLD_DETECT=6 sits roughly halfway between the two, well clear of
// both sides.
// ---------------------------------------------------------------
#define COUNT              0xFFFF
#define THRESHOLD_DETECT        6   // ≥ 6 tenths → no turbo / 1 MHz

// ---------------------------------------------------------------
// Function prototypes
// ---------------------------------------------------------------

int uii_turbo_benchmark_delay(unsigned int count);
/*
  Run a short, hand-written 6502 assembly counting loop (a
  borrow-based 16-bit decrement-to-zero counter -- see ultimate_turbo_lib.c) and
  return elapsed time in CIA1 TOD tenths of a second (10ths; range
  0–99 for <10 s).

  The function uses #pragma optimize(0) and __noinline purely to stop
  the compiler from disturbing the hand-written __asm block or its
  surrounding hi/lo setup -- the cycle cost itself comes from the
  assembly, not from suppressing C optimisation. Each count costs a
  fixed, computable number of cycles (~20.016 avg -- see ultimate_turbo_lib.c),
  which gives CIA TOD enough time to advance with far less run-to-run
  jitter than a compiler-generated loop.

  Resets CIA1 TOD to 00:00.0 on entry and reads it on exit.
  SEI/CLI wraps the measurement. `count` is destroyed by the loop.
*/

char uii_turbo_detect(void);
/*
  Detect whether turbo is genuinely engaged, via CIA TOD timing.

  Forces a clean 1 MHz baseline first (its own settle pass), then sets
  CPU to maximum speed and calls uii_turbo_benchmark_delay(COUNT) twice more --
  once as a throwaway pass to let the FPGA clock-domain change settle,
  once to measure. The elapsed time (CIA1 TOD tenths) is compared
  against a single threshold:

  Returns:
    TURBO_NOT_PRESENT  — elapsed ≥ THRESHOLD_DETECT (≈1 MHz, not engaged)
    TURBO_DETECTED     — elapsed <  THRESHOLD_DETECT (genuinely accelerated)

  Does NOT classify the MHz ceiling -- see the file header for why,
  and how to get that from CTRL_CMD_GET_HWINFO instead.

  The forced 1 MHz baseline pass exists because an auto-loaded .cfg's
  own Turbo Control setting can leave $D031 already at a non-zero speed
  index before this function ever runs -- jumping straight from that
  state to max speed was found to settle unreliably on real Ultimate
  64-II hardware (intermittent false TURBO_NOT_PRESENT, confirmed via
  c64bridge real-hardware testing, 2026-09-23). See ultimate_turbo_lib.c's own
  comment on this function for the full real-hardware finding.

  Restores $D031 to 1 MHz after measuring.
  Call once at startup; worst case (no turbo present) takes ~3.9s at
  1 MHz across the three uii_turbo_benchmark_delay() passes, ~0.05s if turbo is
  genuinely engaged.
*/

void uii_turbo_set(char control);
/*
  Write `control` directly to $D031 and enable via $D030.

  `control` = speed_index | badlines_flag, e.g.:
      uii_turbo_set(TURBO_SPEED_MAX | TURBO_BADLINES_OFF);
      uii_turbo_set(TURBO_FULL);
      uii_turbo_set(TURBO_SPEED_1MHZ);

  Change takes effect on the very next CPU cycle.
*/

void uii_turbo_fast(void);
// Shorthand: uii_turbo_set(TURBO_SPEED_MAX | TURBO_BADLINES_OFF).

void uii_turbo_slow(void);
// Shorthand: uii_turbo_set(TURBO_SPEED_1MHZ).

unsigned char uii_turbo_get(void);  // [UNTESTED]
// Read the current $D031 value (0xFF if registers not available).

// ---------------------------------------------------------------
// Raster-timed speed probe (library 1.2.0)
//
// Classifies the maximum turbo speed as 48 or 64 MHz by timing a fixed
// 64,764-cycle loop against the VIC raster counter ($D012), which
// advances once per real PAL line (64 us) at any CPU speed:
//   64 MHz: 64764 / (63 * 63) = 16.3 lines
//   48 MHz: 64764 / (63 * 47) = 21.9 lines
// (the VIC takes one sub-slot per phi2 cycle, so the CPU gets 63 or 47
// cycles per phi2, not 64 or 48). A loop at 1 MHz spans 1028 lines.
// Based on upic_select_display_path() by Christian Gleissner in
// mandelbrot-upic (include/upic_viewer.c, PR #2); adapted: split into a
// raw measurement and a classifier, interrupts disabled during the loop,
// $D031 restored afterwards, PAL only.
// ---------------------------------------------------------------

#define TURBO_PROBE_START   0x20   // raster line the timed loop starts on
#define TURBO_PROBE_64MHZ   19     // fewer lines: 64 MHz
#define TURBO_PROBE_VALID   26     // fewer lines (and >= 19): 48 MHz; else not a turbo tier

#define TURBO_MAX_UNKNOWN   0      // no stable result (turbo not engaged, or gave up)
#define TURBO_MAX_48MHZ     1      // Ultimate 64 / Elite I at index 15
#define TURBO_MAX_64MHZ     2      // Ultimate 64 Elite II / C64 Ultimate at index 15

unsigned char uii_turbo_probe_lines(char control);
/*
  One measurement: set $D031 to `control` (via uii_turbo_set), wait for
  raster line TURBO_PROBE_START, run the 64,764-cycle loop with
  interrupts disabled and return the number of raster lines it took,
  modulo 256 (16 at 64 MHz, 21-22 at 48 MHz, 92 for a loop entirely at
  1 MHz). Leaves $D031 at `control`. PAL timing.
*/

char uii_turbo_probe_max(void);
/*
  Classify the maximum turbo speed: TURBO_MAX_64MHZ, TURBO_MAX_48MHZ or
  TURBO_MAX_UNKNOWN. Measures at TURBO_FULL until two consecutive loops
  give the same class -- the Ultimate 64 runs the CPU at 1 MHz for a few
  seconds after a reset whatever $D031 says, and a window can end during
  at most one loop. Each loop rewrites $D031. Gives up after 256 loops
  (about 20 s if turbo stays off, e.g. turbo registers disabled in the
  menu). Restores the previous $D031 value before returning.
*/

#pragma compile("ultimate_turbo_lib.c")

#endif
