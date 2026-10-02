// Hardware test of ultimate_upic_lib (Ultimate 64 family, firmware 3.15+,
// PAL). Build with `make upictest`, run build/upictest.prg on the device
// from a writable directory, read the results over REST:
//
//   result[0]  display path from uii_upic_init(UII_UPIC_AUTO): 1 = 48 MHz,
//              2 = 64 MHz, 0 = no turbo
//   result[1]  uii_upic_save() return value (1 = ok)
//   result[2]  uii_upic_load() return value (2 = v1.3 header file)
//   result[3]  1 if the reloaded bitmap's checksum matches the saved one
//   result[4]  1 if the reloaded palette matches
//   result[5]  1 if the reloaded text matches
//   result[6]  1 if uii_upic_getpixel() read back the plotted test pixels
//   result[7]  $A5 when the test finished
//
// The symbol `result` is in build/upictest.map. Afterwards the picture
// is shown with 100 polled frames, then by the raster-IRQ viewer while
// main() counts `mainloops`; uii_upic_framecount counts IRQ frames.
//
// The test pattern: 16 vertical color bars, single-dot white/black
// stripes at both edges (to check the exact pitch on screen: every
// stripe must be one dot wide and the outermost ones visible), text and
// hex digits via the character ROM, and a masked plot rectangle.

#include <string.h>

// Memory layout (see `make upictest`): picture byte columns 0-19 at
// $E000-$F3FF (UII_UPIC_RELOC_COLS=20, ROM banked out), columns 20-191
// at $2400-$CFFF (UII_UPIC_BITMAP=$1000); this program below $2400; the
// generated renderer and nybble table at $F400 (UII_UPIC_GEN=upicgen).
#pragma region(main, 0x0880, 0x2400, , , {code, data, bss, heap, stack})
#pragma stacksize(512)
#pragma heapsize(16)
#pragma section(upicgen, 0)
#pragma region(upicgen, 0xf400, 0xfff0, , , {upicgen})
#include "ultimate_common_lib.h"
#include "ultimate_dos_lib.h"
#include "ultimate_turbo_lib.h"
#include "ultimate_upic_lib.h"

volatile char result[8];
volatile unsigned long mainloops;
// Debug trace: dbg[0] = step reached; then the UCI status text after
// change_dir, save and load (16 bytes each, NUL-terminated).
volatile char dbg[64];

static void trace(char step, char slot)
{
	char i;
	dbg[0] = step;
	if (slot)
		for (i = 0; i < 15; i++)
			dbg[slot * 16 + i] = uii_status[i];
}

static const char palette[48] = {
	0x00,0x00,0x00, 0xff,0xff,0xff, 0x88,0x39,0x32, 0x67,0xb6,0xbd,
	0x8b,0x3f,0x96, 0x55,0xa0,0x49, 0x40,0x31,0x8d, 0xbf,0xce,0x72,
	0x8b,0x54,0x29, 0x57,0x42,0x00, 0xb8,0x69,0x62, 0x50,0x50,0x50,
	0x78,0x78,0x78, 0x94,0xe0,0x89, 0x78,0x69,0xc4, 0x9f,0x9f,0x9f
};

static char text[160];
static char palette_in[48];
static char text_in[160];

static unsigned checksum(void)
{
	unsigned sum = 0;
	char c;
	for (c = 0; c < UII_UPIC_COLUMNS; c++)
	{
		char *p = uii_upic_column(c);
		unsigned i;
		for (i = 0; i < 256; i++)
			sum = (sum << 1 | sum >> 15) ^ p[i];
	}
	return sum;
}

static void pattern(void)
{
	unsigned x;
	char y, i;

	for (x = 0; x < UII_UPIC_WIDTH; x++)
		for (y = 0; y < 255; y++)
			uii_upic_plot(x, y, (char)(x / 24));
	uii_upic_plot(0, 255, 1);

	// One-dot stripes at both edges, rows 0-31.
	for (y = 0; y < 32; y++)
		for (x = 0; x < 8; x++)
		{
			uii_upic_plot(x, y, (x & 1) ? 0 : 1);
			uii_upic_plot(UII_UPIC_WIDTH - 1 - x, y, (x & 1) ? 0 : 1);
		}

	// Text and hex digits in white, then a masked rectangle that must
	// leave byte columns 40-47 (pixels 80-95), rows 100-149, untouched.
	i = uii_upic_putchar(0x15, 20, 60, 1);        // "U" (screen code)
	i = uii_upic_putchar(0x10, i, 60, 1);         // "P"
	i = uii_upic_putchar(0x09, i, 60, 1);         // "I"
	i = uii_upic_putchar(0x03, i, 60, 1);         // "C"
	uii_upic_writehex(0xa5, i + 4, 60, 1);
	uii_upic_clearchar(20, 80);
	uii_upic_set_mask(40, 100, 149);
	for (y = 90; y < 160; y++)
		for (x = 70; x < 110; x++)
			uii_upic_plot_masked(x, y, 0);
	uii_upic_set_mask(0, 0, 0);
}

int main(void)
{
	char ok;

	__asm { sei }
	*(volatile char *)0x01 = 0x35;     // ROMs out: columns 0-19 and the renderer live there
	memset((char *)result, 0, sizeof(result));

	uii_wait_for_uci(5);
	uii_setpalette(palette);
	uii_turbo_fast();
	result[0] = uii_upic_init(UII_UPIC_AUTO);

	pattern();
	result[6] = uii_upic_getpixel(0, 0) == 1 && uii_upic_getpixel(1, 0) == 0
	         && uii_upic_getpixel(383, 0) == 1 && uii_upic_getpixel(384, 0) == 0xff
	         && uii_upic_getpixel(80, 100) == 3 && uii_upic_getpixel(70, 100) == 0;

	trace(1, 0);
	// A writable directory: the UCI's current directory can be the
	// virtual root "/", where files can't be created.
	uii_change_dir((char *)"/usb0");
	if (!UII_SUCCESS)
		uii_change_dir((char *)"/sd");
	uii_get_path();
	{
		char i;
		for (i = 0; i < 15; i++)
			dbg[16 + i] = uii_data[i];
	}
	dbg[0] = 2;
	memset(text, 0x20, 160);
	memcpy(text, "ULTIMATE-UCI-OSCAR64 UPIC TEST", 30);

	{
		unsigned before = checksum();
		unsigned big;
		result[1] = uii_upic_save("upictest.upic", palette, text, 1);
		trace(3, 2);
		uii_upic_clear(0);
		result[2] = uii_upic_load("upictest.upic", palette_in, text_in);
		trace(4, 3);
		result[3] = checksum() == before;
		result[4] = memcmp(palette_in, palette, 48) == 0;
		result[5] = memcmp(text_in, text, 160) == 0;
		// A 1024-byte uii_read_file_to() (two firmware packets) into
		// columns 20-23, which are contiguous: header + columns 0-2.
		uii_open_file(0x01, (char *)"upictest.upic");
		big = uii_read_file_to(uii_upic_column(20), 1024);
		uii_close_file();
		dbg[2] = (char)big; dbg[3] = (char)(big >> 8);
		// That read overwrote columns 20-23 with raw file bytes; load the
		// picture again so the screen shows the clean test pattern.
		uii_upic_load("upictest.upic", 0, 0);
	}
	result[7] = 0xa5;

	// 100 polled frames, then the raster-IRQ viewer with the main
	// program counting in the background (mainloops, read over REST
	// together with uii_upic_framecount). Runs until reset.
	for (ok = 0; ok < 100; ok++)
		uii_upic_show_frame();
	uii_upic_irq_start();
	for (;;)
		mainloops++;
	return 0;
}
