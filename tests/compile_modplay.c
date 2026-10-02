// Compile check for the MOD player (built by `make check`, C64 only, not
// meant to be run). Calls every uii_modplay_* function directly instead of
// taking addresses -- see tests/gen_compile_all.sh. Keep in step with
// include/ultimate_modplay_lib.h.
#include "ultimate_modplay_lib.h"

char mod_name[] = "song.mod";
volatile char sink;

int main(void)
{
	if (uii_modplay_load(mod_name, 0x100000UL) && uii_modplay_init(0x100000UL))
	{
		uii_modplay_set_master_volume(48);
		uii_modplay_set_stereo(1);
		uii_modplay_start();
		uii_modplay_pause();
		uii_modplay_resume();
		sink = uii_modplay_get_order() + uii_modplay_get_pattern() + uii_modplay_get_row()
		     + uii_modplay_get_bpm() + uii_modplay_is_playing();
		uii_modplay_stop();
	}
	return 0;
}
