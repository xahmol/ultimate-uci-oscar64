/*****************************************************************
Ultimate 64 Upic Library -- implementation
ultimate-uci-oscar64 -- https://github.com/xahmol/ultimate-uci-oscar64

Based on Upic v1.3 by Aleksi Eeben (display.s: InitUpic, UpicIRQ,
RenderLine, PatchLine, WaitForTurbo-free variant; drawing.s: Plot,
ActualPlot, GetPixel, ClearChar, PutChar, WriteHex), shared with Xander
Mol in private correspondence, 2026-10-02. Adapted: Oscar64 port; the
line renderer and its per-line patch list are generated at run time from
the column addresses (Aleksi's source uses assembler `repeat` blocks for
a fixed $1000 layout); drawing routines rewritten in C with the same
behaviour. 48 MHz path after Christian Gleissner (mandelbrot-upic PR #2),
adapted to the same exact pitch. See ultimate_upic_lib.h.
******************************************************************/

#include <string.h>
#include "ultimate_common_lib.h"
#include "ultimate_dos_lib.h"
#include "ultimate_turbo_lib.h"
#include "ultimate_upic_lib.h"

// Section hook (library 1.3.0), see the other modules. This module has
// two more sections a project may want elsewhere:
//   UII_UPIC_INIT: code that runs once (uii_upic_init() and the code
//                  generator); default UII_UPIC_CODE
//   UII_UPIC_GEN:  the generated renderer (uii_upic_code[], 2440 bytes,
//                  256-aligned); bss, default UII_UPIC_BSS
//   UII_UPIC_NYB:  the nybble table (256 bytes, 256-aligned); bss,
//                  default UII_UPIC_GEN
#ifndef UII_UPIC_CODE
#define UII_UPIC_CODE code
#endif
#ifndef UII_UPIC_DATA
#define UII_UPIC_DATA data
#endif
#ifndef UII_UPIC_BSS
#define UII_UPIC_BSS bss
#endif
#ifndef UII_UPIC_INIT
#define UII_UPIC_INIT UII_UPIC_CODE
#endif
#ifndef UII_UPIC_GEN
#define UII_UPIC_GEN UII_UPIC_BSS
#endif
#ifndef UII_UPIC_NYB
#define UII_UPIC_NYB UII_UPIC_GEN
#endif
#pragma code(UII_UPIC_CODE)
#pragma data(UII_UPIC_DATA)
#pragma bss(UII_UPIC_NYB)

// Nybble table: uii_upic_nyb[i] = i >> 4, so `lda nyb,x` turns a byte's
// high nybble (the odd pixel) into a $D020 value. Page-aligned so the
// indexed load never crosses a page (the renderer is cycle-exact).
static char uii_upic_nyb[256];
#pragma align(uii_upic_nyb, 256)

#pragma bss(UII_UPIC_GEN)

// Generated line renderer, called once per raster line (see the layout
// in uii_upic_generate()). Page-aligned so the delay loop's branch never
// crosses a page.
char uii_upic_code[UII_UPIC_CODE_SIZE];
#pragma align(uii_upic_code, 256)

#pragma bss(UII_UPIC_BSS)

char uii_upic_turbo = 0x8f;            // TURBO_SPEED_MAX | TURBO_BADLINES_OFF
volatile char uii_upic_framecount;
static char uii_upic_mask_col;         // 0: mask off
static char uii_upic_mask_top, uii_upic_mask_bottom;
const char *uii_upic_font = 0;

// Screen codes for 0-9 and A-F (character ROM order).
static const char uii_upic_hexdefault[16] = {
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
};
const char *uii_upic_hexchars = uii_upic_hexdefault;

// ---------------------------------------------------------------
// Column addresses
// ---------------------------------------------------------------

// __noinline: Oscar64 1.32.273 at -O2 folds an inlined function that
// returns (char *)(integer expression) to a null pointer -- for constant
// and even runtime arguments (uii_upic_column(0) became 0, so file data
// was written over the zero page). A real call computes it correctly.
__noinline char *uii_upic_column(char col)
{
#if UII_UPIC_RELOC_COLS > 0
	if (col < UII_UPIC_RELOC_COLS)
		return (char *)(UII_UPIC_RELOC_BASE + ((unsigned)col << 8));
#endif
	return (char *)(UII_UPIC_BITMAP + ((unsigned)col << 8));
}

// ---------------------------------------------------------------
// Line renderer generator
// ---------------------------------------------------------------
//
// Generated layout in uii_upic_code[] (called with Y = picture row):
//
//   0     patch list: 24 x  lda col,y / sta site      (8 cycles each)
//   144   ldx #delay / dex / bne *-1                   (5 cycles per count)
//   149   pixels (below)
//   end   nop / nop / lda #0 / sta $d020 / rts         (rest of line black)
//
// 64 MHz, per byte column c (two pixels, 16 cycles):
//   ldx col,y / stx $d020 / lda nyb,x / sta $d020
// with every 8th column (c % 8 == 7) starting `ldx #imm` instead (2
// cycles less). The CPU gets 63 cycles per 8-dot phi2 cycle at 64 MHz,
// so 16 pixels in 7 x 16 + 14 = 126 cycles are exactly 16 dots: every
// pixel is one dot wide (Aleksi Eeben's Upic v1.3 timing). The patch
// list copies the current row's byte of those 24 columns into the
// immediate operands before the pixels start.
//
// 48 MHz, per group g of two byte columns A = 2g, B = 2g + 1 (pixels 4g,
// 4g+2 and 4g+3 shown, 4g+1 not; 24 cycles):
//   lda A,y / sta $d020 / ldx B,y / stx $d020 / lda nyb,x / sta $d020
// with every 4th group (g % 4 == 3) starting `lda #imm` instead. The CPU
// gets 47 cycles per phi2 at 48 MHz, so 16 pixels in 3 x 24 + 22 = 94
// cycles are exactly 16 dots, the same pitch as the 64 MHz path.

#pragma code(UII_UPIC_INIT)

static char *gen;

static void gen_op(char op, unsigned operand)
{
	gen[0] = op;
	gen[1] = (char)operand;
	gen[2] = (char)(operand >> 8);
	gen += 3;
}

static void uii_upic_generate(char path)
{
	char *patch = uii_upic_code;        // patch list write pointer
	unsigned nyb = (unsigned)uii_upic_nyb;
	char c;

	gen = uii_upic_code + 144;
	gen[0] = 0xa2;                      // ldx #delay
	gen[1] = path == UII_UPIC_48MHZ ? UII_UPIC_DELAY_48 : UII_UPIC_DELAY_64;
	gen[2] = 0xca;                      // dex
	gen[3] = 0xd0;                      // bne *-1
	gen[4] = 0xfd;
	gen += 5;

	for (c = 0; c < UII_UPIC_COLUMNS; c++)
	{
		unsigned col = (unsigned)uii_upic_column(c);
		char patched;

		if (path == UII_UPIC_48MHZ)
		{
			if (c & 1)
			{
				gen_op(0xbe, col);              // ldx B,y
				gen_op(0x8e, 0xd020);           // stx $d020
				gen_op(0xbd, nyb);              // lda nyb,x
				gen_op(0x8d, 0xd020);           // sta $d020
				continue;
			}
			patched = (c & 7) == 6;             // group g % 4 == 3
			if (!patched)
				gen_op(0xb9, col);              // lda A,y
			else
			{
				gen[0] = 0xa9;                  // lda #imm
				gen += 2;
			}
			gen_op(0x8d, 0xd020);               // sta $d020
		}
		else
		{
			patched = (c & 7) == 7;
			if (!patched)
				gen_op(0xbe, col);              // ldx col,y
			else
			{
				gen[0] = 0xa2;                  // ldx #imm
				gen += 2;
			}
			gen_op(0x8e, 0xd020);               // stx $d020
			gen_op(0xbd, nyb);                  // lda nyb,x
			gen_op(0x8d, 0xd020);               // sta $d020
		}

		if (patched)
		{
			// The immediate operand sits right after the opcode; find it
			// from the end of what was just emitted.
			unsigned site = (unsigned)gen - (path == UII_UPIC_48MHZ ? 4 : 10);
			patch[0] = 0xb9;                    // lda col,y
			patch[1] = (char)col;
			patch[2] = (char)(col >> 8);
			patch[3] = 0x8d;                    // sta site
			patch[4] = (char)site;
			patch[5] = (char)(site >> 8);
			patch += 6;
		}
	}

	// Rest of the line black (some VGA modes show it; HDMI doesn't).
	// Two NOPs first: the black store would otherwise follow the last
	// pixel's store by only 6 cycles -- less than one dot at 64 MHz
	// (7.9 cycles) -- and pixel 383 never showed (measured on an Elite
	// II; Aleksi Eeben's v1.3 RenderLine has the same 6-cycle gap).
	gen[0] = 0xea;                      // nop
	gen[1] = 0xea;                      // nop
	gen[2] = 0xa9;                      // lda #0
	gen[3] = 0x00;
	gen += 4;
	gen_op(0x8d, 0xd020);               // sta $d020
	gen[0] = 0x60;                      // rts
}

char uii_upic_init(char path)
{
	unsigned i;

	for (i = 0; i < 256; i++)
		uii_upic_nyb[i] = (char)(i >> 4);

	if (path == UII_UPIC_AUTO)
	{
		char max = uii_turbo_probe_max();
		path = max == TURBO_MAX_48MHZ ? UII_UPIC_48MHZ
		     : max == TURBO_MAX_64MHZ ? UII_UPIC_64MHZ
		     : UII_UPIC_NONE;
	}
	uii_upic_generate(path == UII_UPIC_48MHZ ? UII_UPIC_48MHZ : UII_UPIC_64MHZ);
	return path;
}

#pragma code(UII_UPIC_CODE)

void uii_upic_set_delay(char delay)
{
	uii_upic_code[145] = delay;
}

// ---------------------------------------------------------------
// Frame renderer (polled)
// ---------------------------------------------------------------
// The timing-critical part is all in uii_upic_code[], whose position is
// known (page-aligned), so this loop can live anywhere: its own branches
// only wait, and the $D031 rewrite on every line resynchronises the CPU
// to the phi2 cycle before the timed code starts (index 0 for the last
// cycles of the second store, so turbo resumes at a fixed point of the
// line whatever the polling jitter was).

__asm uii_upic_frame_asm
{
		lda #$00                // display off: the whole screen is border
		sta $d011
	f1:
		lda $d011               // wait for the bottom of the frame...
		bpl f1
	f2:
		lda $d011               // ...and the raster wrap to line 0
		bmi f2
		lda #$18                // top of the picture (raster line 24)
	tw:
		cmp $d012
		bne tw
		ldy #$00
	line:
		lda $d012
	lw:
		cmp $d012               // wait for the next raster line
		beq lw
		lda #$80                // resync: index 0, then back to turbo
		ldx uii_upic_turbo
		sta $d031
		stx $d031
		jsr uii_upic_code
		iny
		bne line
		rts
}

void uii_upic_show_frame(void)
{
	__asm { jsr uii_upic_frame_asm }
}

// ---------------------------------------------------------------
// Raster-IRQ viewer (Aleksi Eeben's Upic v1.3 InitUpic/UpicIRQ)
// ---------------------------------------------------------------

__asm uii_upic_rts
{
		rts
}

__asm uii_upic_rti
{
		rti
}

void *uii_upic_irq_hook = uii_upic_rts;
#pragma align(uii_upic_irq_hook, 2)     // jmp (hook) must not straddle a page

__asm uii_upic_irq_asm
{
		pha
		txa
		pha
		tya
		pha
		cld
		lda $01
		pha
		lda #$35
		sta $01
		lda #$18                // top of the picture
	tw:
		cmp $d012
		bne tw
		ldy #$00
	line:
		lda $d012
	lw:
		cmp $d012
		beq lw
		lda #$80
		ldx uii_upic_turbo
		sta $d031
		stx $d031
		jsr uii_upic_code
		iny
		bne line
		sty $d020               // rest of the frame black
		inc uii_upic_framecount
		lda #$01                // acknowledge the raster interrupt
		sta $d019
		jsr hook
		pla
		sta $01
		pla
		tay
		pla
		tax
		pla
		rti
	hook:
		jmp (uii_upic_irq_hook)
}

void uii_upic_irq_start(void)
{
	__asm { sei }
	*(volatile char *)0x01 = 0x35;
	*(void **)0xfffa = uii_upic_rti;           // NMI (RESTORE): ignore
	*(void **)0xfffe = uii_upic_irq_asm;
	*(volatile char *)0xd011 = 0x00;           // display off, raster bit 8 = 0
	// No CIA timer interrupts, and acknowledge a pending one: an
	// unacknowledged CIA1 interrupt keeps the IRQ line low, so the CPU
	// re-enters the handler right after every RTI and the main program
	// never runs. In assembly: Oscar64 1.32.273 drops the C read
	// `(void)*(volatile char *)0xdc0d;` despite the volatile.
	__asm {
		lda #$7f
		sta $dc0d
		lda $dc0d
	}
	*(volatile char *)0xd012 = 0x17;           // interrupt one line above the picture
	*(volatile char *)0xd01a = 0x01;
	*(volatile char *)0xd019 = 0x01;
	__asm { cli }
}

void uii_upic_irq_stop(void)
{
	__asm { sei }
	*(volatile char *)0xd01a = 0x00;
	*(volatile char *)0xd019 = 0x01;
}

// ---------------------------------------------------------------
// Drawing (Aleksi Eeben's drawing.s, in C)
// ---------------------------------------------------------------

void uii_upic_plot(unsigned x, char y, char color)
{
	char *p;
	if (x >= UII_UPIC_WIDTH)
		return;
	p = uii_upic_column((char)(x >> 1)) + y;
	if (x & 1)
		*p = (*p & 0x0f) | (char)(color << 4);
	else
		*p = (*p & 0xf0) | (color & 0x0f);
}

char uii_upic_getpixel(unsigned x, char y)
{
	unsigned char v;
	if (x >= UII_UPIC_WIDTH)
		return 0xff;
	v = uii_upic_column((char)(x >> 1))[y];
	return (x & 1) ? v >> 4 : v & 0x0f;
}

void uii_upic_clear(char color)
{
	char c;
	color = (color & 0x0f) | (char)(color << 4);
	for (c = 0; c < UII_UPIC_COLUMNS; c++)
		memset(uii_upic_column(c), color, 256);
}

void uii_upic_set_mask(char col, char top, char bottom)
{
	uii_upic_mask_col = col;
	uii_upic_mask_top = top;
	uii_upic_mask_bottom = bottom;
}

void uii_upic_plot_masked(unsigned x, char y, char color)
{
	if (uii_upic_mask_col)
	{
		unsigned char rel = (unsigned char)(x >> 1) - uii_upic_mask_col;
		if (rel < 8 && y >= uii_upic_mask_top && y <= uii_upic_mask_bottom)
			return;
	}
	uii_upic_plot(x, y, color);
}

char uii_upic_clearchar(char col, char y)
{
	char i;
	for (i = 0; i < 4; i++)
	{
		if (col + i >= UII_UPIC_COLUMNS)
			break;
		memset(uii_upic_column(col + i) + y, 0, 8);
	}
	return col + 4;
}

char uii_upic_putchar(char ch, char col, char y, char color)
{
	char glyph[8];
	char row, bit;

	if (uii_upic_font)
		memcpy(glyph, uii_upic_font + (unsigned)ch * 8, 8);
	else
	{
		// Character ROM: $01 = $31 maps it at $D000 while keeping
		// $A000-$FFFF RAM, so code there (and the caller's picture
		// columns) stay valid. I/O is gone meanwhile; interrupts off.
		const char *src = (const char *)(0xd000 + (unsigned)ch * 8);
		char port;
		__asm { php
				sei }
		port = *(volatile char *)0x01;
		*(volatile char *)0x01 = 0x31;
		for (row = 0; row < 8; row++)
			glyph[row] = src[row];
		*(volatile char *)0x01 = port;
		__asm { plp }
	}

	for (row = 0; row < 8; row++)
	{
		char bits = glyph[row];
		unsigned x = (unsigned)col * 2;
		for (bit = 0; bit < 8; bit++)
		{
			if (bits & 0x80)
				uii_upic_plot(x + bit, y + row, color);
			bits <<= 1;
		}
	}
	return col + 4;
}

char uii_upic_writehex(char value, char col, char y, char color)
{
	col = uii_upic_putchar(uii_upic_hexchars[value >> 4], col, y, color);
	return uii_upic_putchar(uii_upic_hexchars[value & 0x0f], col, y, color);
}

// ---------------------------------------------------------------
// Files
// ---------------------------------------------------------------

// First 48 header bytes: tag, size, slideshow settings, saved time.
static const char uii_upic_header0[48] = {
	0x55, 0x70, 0x69, 0x63, 0x31, 0x2e, 0x33, 0xae,   // "Upic1.3", $AE
	0x80, 0x01, 0x00, 0x01, 0x10, 0x00, 0x00, 0xc0,   // 384, 256, 16, 49152
	0, 0, 0, 0, 0, 0, 0, 0,                           // slideshow settings
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               // last saved time
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

char uii_upic_save(const char *filename, const char *palette, const char *text, char overwrite)
{
	char c;

	uii_open_file(overwrite ? 0x0a : 0x06, (char *)filename);   // 0x0A = FA_WRITE | FA_CREATE_ALWAYS
	if (!UII_SUCCESS)
		return 0;

	uii_write_file_from(uii_upic_header0, 48);
	if (text)
		uii_write_file_from(text, 160);
	else
	{
		// No text: 160 spaces, written 40 at a time from a small stack
		// buffer instead of keeping 160 bytes of spaces in memory.
		char spaces[40];
		memset(spaces, 0x20, 40);
		for (c = 0; c < 4; c++)
			uii_write_file_from(spaces, 40);
	}
	uii_write_file_from(palette, UII_UPIC_PALETTE);

	for (c = 0; c < UII_UPIC_COLUMNS; c++)
		uii_write_file_from(uii_upic_column(c), 256);

	uii_close_file();
	return 1;
}

char uii_upic_load(const char *filename, char *palette, char *text)
{
	char *col0 = uii_upic_column(0);
	char result, c;

	uii_open_file(0x01, (char *)filename);
	if (!UII_SUCCESS)
		return UII_UPIC_FILE_ERROR;

	// The first 256 bytes are either the v1.3 header or bitmap column 0;
	// read them into column 0 either way.
	if (uii_read_file_to(col0, 256) != 256)
	{
		uii_close_file();
		return UII_UPIC_FILE_ERROR;
	}

	if (col0[0] == 0x55 && col0[7] == 0xae)
	{
		if (palette)
			memcpy(palette, col0 + 0xd0, UII_UPIC_PALETTE);
		if (text)
			memcpy(text, col0 + 0x30, 160);
		result = UII_UPIC_FILE_V13;
		c = 0;
	}
	else
	{
		result = UII_UPIC_FILE_RAW;
		c = 1;
	}

	for (; c < UII_UPIC_COLUMNS; c++)
		if (uii_read_file_to(uii_upic_column(c), 256) != 256)
		{
			result = UII_UPIC_FILE_ERROR;
			break;
		}

	uii_close_file();
	return result;
}

#pragma code(code)
#pragma data(data)
#pragma bss(bss)
