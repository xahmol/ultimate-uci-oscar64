# Upic Library Manual

Library files: `include/ultimate_upic_lib.h` / `include/ultimate_upic_lib.c`
(library 1.3.0). Depends on `ultimate_common_lib`, `ultimate_dos_lib` and
`ultimate_turbo_lib`.

Upic is Aleksi Eeben's 384x256 pixel, 16-color picture mode for the
Ultimate 64 family and the C64 Ultimate. With the VIC-II display switched
off, the whole visible area is border, and the CPU at turbo speed changes
the border color once per pixel, timed against the raster beam. Every pixel
has its own color: no character cells, no color clash.

- Upic: <https://csdb.dk/release/?id=263889>
- Upic Image Converter v1.2: <https://csdb.dk/release/?id=264448>

## Contents

1. [Credits](#1-credits)
2. [Requirements](#2-requirements)
3. [Picture layout and configuration](#3-picture-layout-and-configuration)
4. [Display](#4-display)
5. [How the renderer works](#5-how-the-renderer-works)
6. [Drawing](#6-drawing)
7. [Files](#7-files)
8. [Memory placement](#8-memory-placement)
9. [Example](#9-example)
10. [Hardware test](#10-hardware-test)

## 1. Credits

- **Aleksi Eeben**: the Upic technique and format; the line renderer with
  per-line patched immediate operands that gives an exact one-dot pixel
  pitch at 64 MHz; the raster-IRQ viewer; the drawing routines (Upic v1.3
  `display.s` and `drawing.s`, shared with Xander Mol in private
  correspondence, 2026-10-02); the v1.3 file header layout.
- **Christian Gleissner**: the 48 MHz display path (3 of every 4 pixels)
  and the raster-timed speed probe it relies on (`uii_turbo_probe_max()` in
  `ultimate_turbo_lib`), first written for mandelbrot-upic (pull request #2).

This module ports that work to Oscar64: the line renderer is generated at
run time from the picture's column addresses (Aleksi's source uses
assembler `repeat` blocks for a fixed layout), the drawing routines are in
C, and the 48 MHz path gets the same exact pitch as the 64 MHz path.

## 2. Requirements

- An Ultimate 64, Ultimate 64 Elite, Ultimate 64 Elite II or C64 Ultimate
  with the turbo registers enabled (`Turbo Control` = `U64 Turbo Registers`
  or `C64U Turbo Registers`). PAL.
- `uii_turbo_fast()` before `uii_upic_init()`.
- Interrupts disabled while `uii_upic_show_frame()` runs: it is
  cycle-exact. Or use the raster-IRQ viewer instead.
- Picture columns or code under the KERNAL ROM (`$E000`-`$FFFF`) need the
  ROMs banked out by the program (`$01` = `$35`).
- Colors come from the 16-entry palette; set it with `uii_setpalette()`
  (firmware 3.15+ / C64U firmware with palette control).

## 3. Picture layout and configuration

The picture is 192 byte columns of 256 bytes. Byte column `c` holds the
pixels `x = 2c` (low nybble) and `x = 2c + 1` (high nybble) for rows
0-255, column-major: the same layout as the `.upic` files of the Upic
Image Converter.

Column `c` lives at `UII_UPIC_BITMAP + c * 256`. With the default
`UII_UPIC_BITMAP` = `$1000` that is Aleksi's standard layout, `$1000`-
`$CFFF`. A program whose own code occupies part of that range can move the
first `UII_UPIC_RELOC_COLS` columns to `UII_UPIC_RELOC_BASE + c * 256`
instead; mandelbrot-upic keeps columns 0-7 at `$E000`.

Configuration is by compiler defines, because the library's `.c` file is a
separate translation unit (a `#define` in the program's own source does not
reach it):

| Define | Default | Meaning |
|---|---|---|
| `UII_UPIC_BITMAP` | `0x1000` | Address of byte column 0 |
| `UII_UPIC_RELOC_COLS` | `0` | Number of leading columns stored elsewhere |
| `UII_UPIC_RELOC_BASE` | `0xE000` | Their base address (+ `c * 256`) |
| `UII_UPIC_DELAY_64` | `83` | Line delay count, 64 MHz path |
| `UII_UPIC_DELAY_48` | `52` | Line delay count, 48 MHz path |

Example: `-dUII_UPIC_RELOC_COLS=8 -dUII_UPIC_RELOC_BASE=0xE000`.

`uii_upic_column(col)` returns a column's address. It is `__noinline` on
purpose: Oscar64 1.32.273 at `-O2` folds an inlined function returning
`(char *)(integer expression)` to a null pointer.

## 4. Display

```c
char uii_upic_init(char path);
void uii_upic_show_frame(void);
void uii_upic_set_delay(char delay);
extern char uii_upic_turbo;
void uii_upic_irq_start(void);
void uii_upic_irq_stop(void);
extern volatile char uii_upic_framecount;
extern void *uii_upic_irq_hook;
```

**`uii_upic_init(path)`** builds the nybble table and generates the line
renderer. `path` is `UII_UPIC_64MHZ`, `UII_UPIC_48MHZ`, or `UII_UPIC_AUTO`
to measure the CPU's top speed with `uii_turbo_probe_max()` first. It
returns the path built, or `UII_UPIC_NONE` when the probe found no turbo
(the 64 MHz renderer is built then). Call it once before the first frame.

| Path | Machines | Pixels shown |
|---|---|---|
| `UII_UPIC_64MHZ` | Ultimate 64 Elite II, C64 Ultimate | all 384, one dot each |
| `UII_UPIC_48MHZ` | Ultimate 64, Ultimate 64 Elite | 3 of every 4: pixel 4m two dots wide, 4m+1 not shown, 4m+2 and 4m+3 one dot each |

Both paths put every pixel on the same dots: the picture fills the
384-dot visible area exactly on both.

**`uii_upic_show_frame()`** draws one frame: display off (`$D011` = 0),
wait for the frame start, draw 256 lines from raster line `$19`. It returns
after the last line; call it in a loop and do other work between calls
(about 18% of the frame is free).

**`uii_upic_set_delay(d)`** changes the delay count before each line's
first pixel (5 cycles per count) for the current path, to move the picture
horizontally. The defaults put pixel 0 on the first visible dot.

**`uii_upic_turbo`** is the `$D031` value written on every line (default
`$8F`: top speed, badlines off). A test can set `$8E` and build the
48 MHz path to see it on an Elite II (speed index 14 is 48 MHz there).

**`uii_upic_irq_start()`** shows the picture continuously from a raster
interrupt, as Aleksi Eeben's Upic v1.3 viewer does: it banks the ROMs out
(`$01` = `$35`), points the IRQ vector at the renderer and the NMI vector
at an `RTI`, switches CIA1 timer interrupts off (and acknowledges a pending
one) and enables a raster interrupt at line `$17`. The main program then
runs during the remaining ~18% of every frame. `uii_upic_framecount` counts
IRQ frames; `uii_upic_irq_hook` (default: nothing) is called at the end of
each IRQ frame, with the ROMs out. **`uii_upic_irq_stop()`** switches the
raster interrupt off and leaves interrupts disabled.

## 5. How the renderer works

Each raster line, the frame loop waits for the line to start, writes `$80`
then `uii_upic_turbo` to `$D031` (index 0 for the last cycles of the second
store, so turbo resumes at the same point of every line whatever the
polling jitter was), and calls the generated code, which is page-aligned so
its timing does not depend on where the linker puts anything else:

| Part | Code | Cycles |
|---|---|---|
| Patch list | 24 x `lda col,y` / `sta site` | 192 |
| Delay | `ldx #d` / `dex` / `bne` | 5 per count |
| Pixels | see below | 3024 (64 MHz) / 2256 (48 MHz) |
| End | `nop` / `nop` / `lda #0` / `sta $d020` / `rts` | |

**64 MHz:** per byte column (two pixels, 16 cycles)
`ldx col,y` / `stx $d020` / `lda nyb,x` / `sta $d020`, where `nyb[i] = i >> 4`.
The CPU gets 63 cycles per 8-dot phi2 cycle, so 8 cycles per pixel would be
64/63 of a dot. Every 8th column starts with `ldx #imm` instead (2 cycles
less), whose operand the patch list fills with the current row's byte:
16 pixels take 126 cycles, exactly 16 dots.

**48 MHz:** per group of byte columns A = 2g, B = 2g + 1 (24 cycles)
`lda A,y` / `sta $d020` / `ldx B,y` / `stx $d020` / `lda nyb,x` / `sta $d020`.
The first store shows pixel 4g (the low nybble of A; `$D020` uses 4 bits)
until the second, so it covers 4g and 4g+1. The CPU gets 47 cycles per
phi2; every 4th group starts with `lda #imm` (2 cycles less), so 16 pixels
take 94 cycles, exactly 16 dots.

The end of the line has two `NOP`s before the black store: without them it
followed the last pixel's store by only 6 cycles, less than one dot at
64 MHz, and pixel 383 never showed. Aleksi's v1.3 `RenderLine` has the same
6-cycle gap.

Measured on an Ultimate 64 Elite II (64 MHz) and an Ultimate 64 Elite
(48 MHz), firmware 3.15a, from the VIC video stream: pixel 0 on dot 0,
pixel 383 on dot 383, single-dot stripes resolved at both edges, and every
24-pixel color bar edge on the same dot on both machines, with the polled
renderer and with the IRQ viewer (8 identical consecutive frames).

## 6. Drawing

```c
__noinline char *uii_upic_column(char col);
void uii_upic_plot(unsigned x, char y, char color);
char uii_upic_getpixel(unsigned x, char y);
void uii_upic_clear(char color);
void uii_upic_set_mask(char col, char top, char bottom);
void uii_upic_plot_masked(unsigned x, char y, char color);
char uii_upic_clearchar(char col, char y);
char uii_upic_putchar(char ch, char col, char y, char color);
char uii_upic_writehex(char value, char col, char y, char color);
extern const char *uii_upic_font;
extern const char *uii_upic_hexchars;
```

Coordinates: `x` 0-383, `y` 0-255, `color` 0-15. Text positions use byte
columns (`col` 0-191, two pixels each): an 8x8 character covers 4 byte
columns.

| Function | Does |
|---|---|
| `uii_upic_plot` | Set one pixel; ignores `x` >= 384 |
| `uii_upic_getpixel` | Read one pixel; `$FF` outside the picture |
| `uii_upic_clear` | Fill the whole picture with one color |
| `uii_upic_set_mask` / `uii_upic_plot_masked` | Plot that leaves byte columns `col`..`col+7`, rows `top`..`bottom` untouched (Upic Paint's tool panel); `col` = 0 switches the mask off |
| `uii_upic_clearchar` | Clear an 8x8 cell to color 0; returns `col + 4` |
| `uii_upic_putchar` | Draw an 8x8 character, set bits only; returns `col + 4` |
| `uii_upic_writehex` | Two hex digits; returns the next column |

Characters come from `uii_upic_font` (8 bytes per character) or, when that
is NULL, from the character ROM by screen code. The ROM is read with `$01` =
`$31`, which keeps `$A000`-`$FFFF` RAM, so code and picture columns there
stay valid; interrupts are off meanwhile. `uii_upic_hexchars` holds the 16
character codes for hex digits (default: screen codes `0`-`9`, `A`-`F`).

## 7. Files

```c
char uii_upic_save(const char *filename, const char *palette, const char *text, char overwrite);
char uii_upic_load(const char *filename, char *palette, char *text);
```

Files are read and written in the current UCI directory
(`uii_change_dir()`); after a reset that can be the virtual root `/`, where
files can't be created (`PATH DOESN'T EXIST`), so change to a real
directory first. Transfers go through `uii_write_file_from()` and
`uii_read_file_to()`, 256 bytes at a time: no heap and no large data queue
are needed.

**Format (Upic v1.3):** a 256-byte header followed by the 49152-byte bitmap
in column order (49408 bytes in total):

| Offset | Size | Contents |
|---|---|---|
| `$00` | 8 | `"Upic1.3"` (ASCII) and `$AE` |
| `$08` | 8 | width 384, height 256, colors 16, bitmap size 49152 (16-bit little endian each) |
| `$10` | 8 | slideshow settings (0) |
| `$18` | 24 | last saved time (0) |
| `$30` | 160 | four lines of 40 characters of text |
| `$D0` | 48 | palette, 16 x RGB |
| `$100` | 49152 | bitmap |

The layout is the 256-byte block at `$0F00` in Aleksi Eeben's Upic v1.3
viewer (palette at `$0FD0`, picture from `$1000`). The Upic Image Converter
v1.2 writes `.upic` as the bare 49152-byte bitmap with the palette in a
separate `.pal` file.

**`uii_upic_save`** writes a v1.3 file. `palette`: 48 bytes. `text`: 160
characters, or NULL for spaces. `overwrite`: 0 fails if the file exists
(open attribute `0x06`), 1 replaces it (`0x0A`). Returns 1 on success, 0 on
error (`uii_status` holds the firmware's message).

**`uii_upic_load`** reads a v1.3 file (recognised by `U` and `$AE` at offsets
0 and 7) or a bare 49152-byte bitmap. `palette` and `text`, when not NULL,
receive the header's contents (unchanged for a bare bitmap). Returns
`UII_UPIC_FILE_V13`, `UII_UPIC_FILE_RAW` or `UII_UPIC_FILE_ERROR`. It reads
the first 256 bytes into column 0 to decide, so column 0 is used as the
header buffer.

## 8. Memory placement

Besides the section hook every module has (`UII_UPIC_CODE`, `_DATA`,
`_BSS`, see the UCI manual), this module has two more:

| Define | Default | Holds |
|---|---|---|
| `UII_UPIC_INIT` | `UII_UPIC_CODE` | Code that runs once: `uii_upic_init()` and the generator |
| `UII_UPIC_GEN` | `UII_UPIC_BSS` | The generated renderer (`uii_upic_code[]`, 2440 bytes) and the nybble table (256 bytes), both page-aligned |

Code that runs while the picture is shown must be in normal RAM or under
the KERNAL ROM with the ROMs out, not under I/O (`$D000`-`$DFFF`).

## 9. Example

```c
#include "ultimate_common_lib.h"
#include "ultimate_turbo_lib.h"
#include "ultimate_upic_lib.h"

int main(void)
{
    __asm { sei }
    uii_wait_for_uci(5);
    uii_setpalette(my_palette);         // 48 bytes
    uii_turbo_fast();
    uii_upic_init(UII_UPIC_AUTO);       // 48 or 64 MHz path
    uii_upic_load("picture.upic", my_palette, 0);
    uii_setpalette(my_palette);
    for (;;)
        uii_upic_show_frame();
}
```

## 10. Hardware test

`make upictest` builds `build/upictest.prg` (`tests/upic_test.c`), which
uses the relocated layout (columns 0-19 at `$E000`, the rest from `$2400`).
It chooses the path, draws a test pattern (color bars, single-dot stripes at
both edges, text, hex digits, a cleared cell and a masked rectangle), saves
it as `upictest.upic` in `/usb0` (or `/sd` when there is no USB storage), clears the picture, loads
the file back and compares checksum, palette and text, reads 1024 bytes in
one `uii_read_file_to()` call, then shows 100 polled frames and switches to
the IRQ viewer. Results are in `result[]` and `dbg[]` (addresses in
`build/upictest.map`), read over the REST API.

Passed on 2026-10-02 on an Ultimate 64 Elite II (64 MHz path) and an
Ultimate 64 Elite (48 MHz path), firmware 3.15a.
