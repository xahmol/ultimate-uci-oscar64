// Hardware test of the Heartbeat Soundtracker player (ultimate_hbplay_lib),
// C64 on an Ultimate 64 / C64 Ultimate. `make hbplaytest` builds
// build/hbplaytest.prg.
//
// Needs: firmware 3.15+, Ultimate Audio mapped at $DF20, a 16 MB REU, the
// turbo registers and the Command Interface enabled (heartbeat-demo's
// config/Heartbeat-U64E2.cfg has all settings), and a Heartbeat song named
// SONG.REU in idi8b/hbtest/ on any SD/USB drive (or in the UCI home
// directory) -- for example the Knight Rider theme that comes with
// Heartbeat Soundtracker's evaluation release, renamed. Starting a PRG
// does not change the UCI's current directory, so the test searches the
// drives for that folder (uii_scan_media/uii_find_media_path), as
// heartbeat-demo does for its own.
//
// Plays the song and shows the sequencer position; any key stops it.
// Results, readable over the REST API at the address of `result` (see
// build/hbplaytest.lbl):
//   result[0]  1 if Ultimate Audio was detected
//   result[1]  uii_hbplay_load() return value (1 = ok)
//   result[2]  uii_hbplay_detect_ntsc() (0 = PAL)
//   result[3]  the song's starting tempo (BPM - 64)
//   result[4]  1 once the pattern step has advanced (rows are played)
//   result[5]  1 once a note event reached the visualizer queue
//   result[6]  $AA when the test has finished (after a key press)
#include <stdio.h>
#include <c64/vic.h>
#include <conio.h>
#include "ultimate_common_lib.h"
#include "ultimate_dos_lib.h"
#include "ultimate_turbo_lib.h"
#include "ultimate_audio_lib.h"
#include "ultimate_hbplay_lib.h"

volatile char result[8];

// The player updates its state from the interrupt, and the state is not
// declared volatile: read it through a volatile pointer, or Oscar64 may keep
// an old value in a register for the whole loop.
#define HB_LIVE(field) (*(volatile unsigned char *)&uii_hbplay_state.field)
char song[] = "SONG.REU";
char song_dir[] = "idi8b/hbtest/";
char drives[UII_MAX_DRIVES][UII_DRIVE_PATH_LEN];
char found[UII_DRIVE_PATH_LEN + sizeof(song_dir)];

int main(void)
{
	char first_step;

	printf("HEARTBEAT PLAYER TEST, LIB %s\n", UII_LIB_VERSION);
	uii_turbo_fast();
	result[0] = uii_audio_detect();
	printf("ULTIMATE AUDIO: %s\n", result[0] ? "YES" : "NO");

	{
		char count = 0;
		uii_scan_media(drives, &count);
		if (uii_find_media_path(drives, count, song_dir, found, sizeof(found)))
			printf("FOUND %s\n", found);
	}
	result[1] = uii_hbplay_load(song, UII_HBPLAY_SONG_REU_BASE);
	printf("LOAD %s: %s\n", song, result[1] ? "OK" : "FAILED");
	if (!result[1])
	{
		result[6] = 0xaa;
		return 0;
	}

	result[2] = uii_hbplay_detect_ntsc();
	result[3] = uii_hbplay_songdata.starting_tempo;
	printf("%s, TEMPO %d BPM\n", result[2] ? "NTSC" : "PAL", result[3] + 64);

	uii_hbplay_init(0, 1);
	first_step = HB_LIVE(patt_step);
	printf("PLAYING, ANY KEY STOPS\n\n");

	while (!kbhit())
	{
		vic_waitFrame();
		if (HB_LIVE(patt_step) != first_step)
			result[4] = 1;
		if (*(volatile unsigned char *)&uii_hbplay_vis_event_count)
		{
			result[5] = 1;
			uii_hbplay_vis_event_count = 0;
		}
		// PETSCII 145 = cursor up: rewrite the same line (on the C64 "\r" is
		// RETURN and would scroll).
		printf("%cSEQ %3d ROW %3d SYNC %3d\n", 145, HB_LIVE(seq_step),
		       HB_LIVE(patt_step), *(volatile unsigned char *)&uii_hbplay_ext_out);
	}
	getch();          // one key, no echo (getchar() is KERNAL line input)
	uii_hbplay_stop_all();
	printf("\nSTOPPED. ROWS %s, NOTES %s\n", result[4] ? "OK" : "NONE", result[5] ? "OK" : "NONE");
	result[6] = 0xaa;
	return 0;
}
