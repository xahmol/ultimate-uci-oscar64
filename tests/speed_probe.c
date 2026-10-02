// Hardware test for the raster-timed speed probe (ultimate_turbo_lib).
// Build with `make speedprobe`, start on an Ultimate 64 (PAL), read the
// screen after about 15 seconds. Prints:
//   1. per half second since start: min-max lines of the probe loop at
//      TURBO_FULL -- shows the forced 1 MHz window after a reset (92 lines)
//      and the time it ends;
//   2. five loops each at speed index 15 and 14 (64 and 48 MHz on an
//      Elite II / C64U; 48 and 40 MHz on a U64 / Elite I);
//   3. uii_turbo_probe_max() and how long it took.
#include <stdio.h>
#include <c64/cia.h>
#include "ultimate_turbo_lib.h"

#define BUCKETS 24                      // 24 x 0.5 s = 12 s

unsigned char bmin[BUCKETS], bmax[BUCKETS], bcount[BUCKETS];

static unsigned tod_tenths(void)
// Tenths of a second since the TOD clock was cleared (BCD registers).
{
	unsigned char s = cia1.tods;
	unsigned char t = cia1.todt;        // reading tenths unlatches
	return ((s >> 4) * 10 + (s & 0x0f)) * 10 + t;
}

int main(void)
{
	unsigned char i, b, l;
	unsigned t;

	for (b = 0; b < BUCKETS; b++)
	{
		bmin[b] = 255;
		bmax[b] = 0;
		bcount[b] = 0;
	}

	cia1.todh = 0;                      // stops the clock until tenths are written
	cia1.todm = 0;
	cia1.tods = 0;
	cia1.todt = 0;

	while ((t = tod_tenths()) < BUCKETS * 5)
	{
		l = uii_turbo_probe_lines(TURBO_FULL);
		b = t / 5;
		if (l < bmin[b]) bmin[b] = l;
		if (l > bmax[b]) bmax[b] = l;
		bcount[b]++;
	}

	printf("SPEED PROBE  LINES PER 0.5S (MIN-MAX N)\n");
	for (b = 0; b < BUCKETS; b++)
		printf("%2u.%u:%3u-%3u %3u%s", b / 2, (b & 1) * 5, bmin[b], bmax[b], bcount[b], (b & 1) ? "\n" : "  ");

	printf("IDX15:");
	for (i = 0; i < 5; i++)
		printf(" %u", uii_turbo_probe_lines(TURBO_SPEED_MAX | TURBO_BADLINES_OFF));
	printf("\nIDX14:");
	for (i = 0; i < 5; i++)
		printf(" %u", uii_turbo_probe_lines(TURBO_SPEED_48MHZ | TURBO_BADLINES_OFF));

	t = tod_tenths();
	b = uii_turbo_probe_max();
	t = tod_tenths() - t;
	printf("\nPROBE_MAX: %s IN %u.%u S\nD031 AFTER: %02X\nDONE\n",
		b == TURBO_MAX_64MHZ ? "64 MHZ" : b == TURBO_MAX_48MHZ ? "48 MHZ" : "UNKNOWN",
		t / 10, t % 10, uii_turbo_get());

	uii_turbo_slow();
	return 0;
}
