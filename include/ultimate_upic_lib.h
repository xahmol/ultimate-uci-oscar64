/*****************************************************************
Ultimate 64 Upic Library
ultimate-uci-oscar64 -- https://github.com/xahmol/ultimate-uci-oscar64

Upic is Aleksi Eeben's 384x256 pixel, 16-color picture mode for the
Ultimate 64 family and the C64 Ultimate: with the VIC-II display
disabled ($D011 bit 4 off) the whole visible area is border, and the CPU
at turbo speed changes the border color ($D020) once per pixel, timed
against the raster beam. Every pixel has its own color; there are no
character cells and no color clash. Upic, the Upic Image Converter and
Upic Paint are by Aleksi Eeben: https://csdb.dk/release/?id=263889 (Upic),
https://csdb.dk/release/?id=264448 (Upic Image Converter v1.2).

This module (library 1.3.0) contains:
  - display: a frame renderer polled by the caller, and a raster-IRQ
    variant that shows the picture every frame in the background
  - an exact pixel pitch at 64 MHz (Elite II / C64 Ultimate) and at
    48 MHz (original Ultimate 64 / Elite I, 3 of every 4 pixels shown)
  - drawing: plot, read pixel, masked plot, clear, 8x8 text, hex output
  - files: save and load .upic pictures via UCI file I/O, Upic v1.3
    header format (palette and four text lines in the file)

Credits:
  - Display technique, line renderer with per-line patched immediate
    operands, raster-IRQ viewer and drawing routines: Aleksi Eeben,
    Upic v1.3 (display.s, drawing.s, shared with Xander Mol in private
    correspondence, 2026-10-02). Adapted: ported to Oscar64 C and inline
    assembly; the line renderer is generated at run time from the
    picture's column addresses instead of being an assembler `repeat`
    listing, so any column layout works; drawing routines in C.
  - 48 MHz path (3 of every 4 pixels) and the raster-timed speed probe
    it relies on (uii_turbo_probe_max() in ultimate_turbo_lib): Christian
    Gleissner, mandelbrot-upic pull request #2. Adapted: given the same
    exact pitch as the 64 MHz path by patching every 4th group per line.

Requirements:
  - Turbo registers enabled (Turbo Control "U64 Turbo Registers" or
    "C64U Turbo Registers"); PAL.
  - Interrupts disabled while uii_upic_show_frame() runs (the polled
    renderer is cycle-exact), or the raster-IRQ viewer in use instead.
  - Picture columns or code located under the KERNAL ROM ($E000-$FFFF)
    need the ROM banked out by the caller.

Picture layout: 192 byte columns of 256 bytes. Byte column c holds the
pixels x = 2c (low nybble) and x = 2c + 1 (high nybble) for rows 0-255,
column-major, exactly as the Upic Image Converter writes its .upic files.
Column c lives at UII_UPIC_BITMAP + c * 256, except that the first
UII_UPIC_RELOC_COLS columns may live at UII_UPIC_RELOC_BASE + c * 256
instead (for programs whose own code occupies part of $1000-$CFFF).
See docs/UPIC_MANUAL.md.
******************************************************************/

#ifndef _ULTIMATE_UPIC_LIB_H_
#define _ULTIMATE_UPIC_LIB_H_

// ---------------------------------------------------------------
// Build-time configuration (define with -d on the compiler command line;
// the library's .c file is a separate translation unit, so a #define in
// the project's own source does not reach it)
// ---------------------------------------------------------------

#ifndef UII_UPIC_BITMAP
#define UII_UPIC_BITMAP     0x1000  // address of byte column 0 (Upic standard)
#endif
#ifndef UII_UPIC_RELOC_COLS
#define UII_UPIC_RELOC_COLS 0       // first N columns stored elsewhere...
#endif
#ifndef UII_UPIC_RELOC_BASE
#define UII_UPIC_RELOC_BASE 0xE000  // ...at this address + c * 256
#endif

// Line timing: the delay-loop count before the first pixel of each line,
// per path. Chosen so pixel 0 starts on the first dot of the 384-dot
// visible area and both paths place every pixel on the same dots. Can
// be changed at run time with uii_upic_set_delay().
#ifndef UII_UPIC_DELAY_64
#define UII_UPIC_DELAY_64   83
#endif
#ifndef UII_UPIC_DELAY_48
#define UII_UPIC_DELAY_48   52
#endif

// Size of the generated line renderer (uii_upic_code[]): the 64 MHz path
// needs 2437 bytes, the 48 MHz path 1861.
#define UII_UPIC_CODE_SIZE  2440

#define UII_UPIC_WIDTH      384
#define UII_UPIC_HEIGHT     256
#define UII_UPIC_COLUMNS    192
#define UII_UPIC_BYTES      49152   // bitmap size
#define UII_UPIC_HEADER     256     // v1.3 file header size
#define UII_UPIC_PALETTE    48      // 16 x RGB

// ---------------------------------------------------------------
// Display
// ---------------------------------------------------------------

#define UII_UPIC_AUTO   0   // uii_upic_init(): measure with uii_turbo_probe_max()
#define UII_UPIC_48MHZ  1   // 3 of every 4 pixels (Ultimate 64 / Elite I)
#define UII_UPIC_64MHZ  2   // every pixel (Elite II / C64 Ultimate)
#define UII_UPIC_NONE   0   // returned when AUTO found no turbo (64 MHz code built)

char uii_upic_init(char path);
/*
  Build the nybble table and generate the line renderer for a display
  path: UII_UPIC_64MHZ, UII_UPIC_48MHZ, or UII_UPIC_AUTO to measure the
  CPU's top speed with uii_turbo_probe_max() first (enable turbo with
  uii_turbo_fast() before; the probe can take up to ~20 s at 1 MHz).
  Returns the path built, or UII_UPIC_NONE when AUTO got no result (the
  64 MHz renderer is built then, as the best guess). Call once, before
  the first frame; again to switch paths.
*/

void uii_upic_show_frame(void);
/*
  Render one frame: switches the display off ($D011 = 0), waits for the
  frame start and draws all 256 lines, cycle-exact. Interrupts must be
  disabled. Returns at raster line ~$18 + 256; call it in a loop and do
  other work (keyboard polling, computing) between calls.
*/

void uii_upic_set_delay(char delay);                // [UNTESTED]
/*
  Override the per-line delay-loop count of the current path (5 cycles
  per count). For tuning the picture's horizontal position.
*/

void uii_upic_set_window(char first, char rows);
/*
  Show only picture rows first..first+rows-1 (rows 0 = up to row 255), at
  their normal screen position; everything else stays black. Applies to
  uii_upic_show_frame() and to the raster-IRQ viewer (whose interrupt
  moves to one line above the window, so a narrow window costs little
  CPU: about 1/312 of the frame per row). Default: the whole picture,
  uii_upic_set_window(0, 0). Safe to call while the IRQ viewer runs.
  Library 1.3.0 (idea: Aleksi Eeben -- a narrow window as a progress bar
  while a picture is drawn).
*/

extern char uii_upic_turbo;
/*
  The $D031 value written on every line (default $8F: top speed, no
  badlines). A test build can use $8E with UII_UPIC_48MHZ to show the
  48 MHz path on an Elite II / C64 Ultimate (index 14 is 48 MHz there).
*/

extern char uii_upic_code[UII_UPIC_CODE_SIZE];  // the generated renderer

void uii_upic_irq_start(void);
/*
  Show the picture continuously from a raster interrupt (Aleksi Eeben's
  Upic v1.3 viewer): banks the ROMs out ($01 = $35), points the IRQ
  vector at the renderer and the NMI vector at an RTI, disables CIA
  timer interrupts and enables a raster interrupt at line $17. The
  picture then costs about 82% of the CPU (256 of 312 lines); the
  remaining time belongs to the main program. Call uii_upic_init()
  first.
*/

void uii_upic_irq_stop(void);                   // [UNTESTED]
/*
  Disable the raster interrupt. Leaves interrupts disabled and the ROMs
  banked out; restoring them is up to the caller.
*/

extern volatile char uii_upic_framecount;       // incremented every IRQ frame
extern void *uii_upic_irq_hook;
/*
  Called (jsr) at the end of every IRQ frame, with the ROMs banked out,
  before the interrupt returns. Default: does nothing. Must be an
  assembly routine or a __interrupt-safe function that preserves the
  zero page it uses.
*/

// ---------------------------------------------------------------
// Drawing (x = 0..383, y = 0..255, color = 0..15)
// ---------------------------------------------------------------

__noinline char *uii_upic_column(char col);     // address of byte column 0..191
void uii_upic_plot(unsigned x, char y, char color);
char uii_upic_getpixel(unsigned x, char y);     // color, or $FF outside the picture
void uii_upic_clear(char color);                // fill the whole picture

void uii_upic_set_mask(char col, char top, char bottom);
void uii_upic_plot_masked(unsigned x, char y, char color);
/*
  Plot that leaves a rectangle untouched: byte columns col..col+7
  (16 pixels), rows top..bottom. col = 0 disables the mask. Upic Paint
  keeps its tool panel there.
*/

char uii_upic_clearchar(char col, char y);
/*
  Clear an 8x8 pixel cell: byte columns col..col+3, rows y..y+7, to
  color 0. Returns col + 4 (the next cell).
*/

char uii_upic_putchar(char ch, char col, char y, char color);
/*
  Draw character ch (8x8, set bits only, background untouched) at byte
  column col, row y. Returns col + 4. The glyph comes from uii_upic_font
  (8 bytes per character), or from the character ROM when that is NULL
  (screen codes, read with $01 = $31 so code under the KERNAL ROM area
  keeps running).
*/

char uii_upic_writehex(char value, char col, char y, char color);
/*
  Two hex digits via uii_upic_putchar(), using uii_upic_hexchars (default:
  screen codes for 0-9 and A-F). Returns the column after them.
*/

extern const char *uii_upic_font;               // NULL = character ROM
extern const char *uii_upic_hexchars;           // 16 character codes

// ---------------------------------------------------------------
// Files (.upic, Upic v1.3 format)
// ---------------------------------------------------------------
// A v1.3 file is a 256-byte header followed by the 49152-byte bitmap
// in column order. Header (offsets in hex):
//   00-07 "Upic1.3" (ASCII) and $AE      08-0F width 384, height 256,
//   10-17 slideshow settings (0)               colors 16, bitmap 49152
//   18-2F last saved time (0)                  (16-bit little endian)
//   30-CF four lines of 40 characters of text
//   D0-FF palette, 16 x RGB
// Layout from Aleksi Eeben's Upic v1.3 viewer (display.s, $0F00-$0FFF).

#define UII_UPIC_FILE_ERROR 0
#define UII_UPIC_FILE_RAW   1   // loaded a bare 49152-byte bitmap (v1.2 converter)
#define UII_UPIC_FILE_V13   2   // loaded a file with a v1.3 header

char uii_upic_save(const char *filename, const char *palette, const char *text, char overwrite);
/*
  Save the picture as a v1.3 .upic file in the current UCI directory.
  palette: 48 bytes (16 x RGB). text: 160 characters (4 lines of 40), or
  NULL for 160 zero bytes. overwrite: 0 fails if the file exists, 1 replaces it.
  Returns 1 on success, 0 on error (uii_status holds the firmware's
  message). Uses uii_write_file_from(), so no heap or large data queue.
*/

char uii_upic_load(const char *filename, char *palette, char *text);
/*
  Load a .upic file into the picture: a v1.3 file (header recognised by
  "U" and $AE at offsets 0 and 7) or a bare bitmap as the v1.2 converter
  writes it. palette (48 bytes) and text (160) receive the header's
  contents when not NULL; for a bare bitmap they are left unchanged.
  Returns UII_UPIC_FILE_V13, UII_UPIC_FILE_RAW or UII_UPIC_FILE_ERROR.
  Uses column 0 of the picture as the header buffer.
*/

#pragma compile("ultimate_upic_lib.c")

#endif
