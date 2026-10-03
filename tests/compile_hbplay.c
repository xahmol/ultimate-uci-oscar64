// Compile check for the Heartbeat Soundtracker player (built by `make
// check`, C64 only, not meant to be run). Calls every uii_hbplay_*
// function directly instead of taking addresses -- see
// tests/gen_compile_all.sh (same reason as the MOD player: the player's
// tick runs from an interrupt). Keep in step with
// include/ultimate_hbplay_lib.h.
#include "ultimate_hbplay_lib.h"

char song_name[] = "song.reu";
volatile char sink;

int main(void)
{
	if (uii_hbplay_load(song_name, UII_HBPLAY_SONG_REU_BASE))
	{
		sink = uii_hbplay_detect_ntsc();
		uii_hbplay_init(0, 1);
		uii_hbplay_set_tempo(60);
		uii_hbplay_play_fx(6, 1, 0x26);
		uii_hbplay_stop_fx(6);
		uii_hbplay_fetch_pattern_row();
		uii_hbplay_vis_reset();
		sink = uii_hbplay_ext_out + uii_hbplay_vis_event_count + uii_hbplay_row_buf[0];
		uii_hbplay_stop_all();
	}
	return 0;
}
