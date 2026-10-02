#include <stdio.h>
#include <petscii.h>
#include <c64/charwin.h>
#include "ultimate_common_lib.h"
#include "ultimate_dos_lib.h"
#include "ultimate_time_lib.h"
#include "ultimate_network_lib.h"
#include "ultimate_softiec_lib.h"
#include "ultimate_http_lib.h"

int main(void)
{
	putchar(14);
	printf("ultimate-uci-oscar64 v%s\n", UII_LIB_VERSION);

    // Is Ultimate Command Interface detected? If no, abort
	if (!uii_detect())
	{
		printf("No Ultimate Command Interface enabled.\n");
		printf("Press key to exit.\n");
		cwin_getch();
	}
	else
	{
		printf("Ultimate Command Interface detected.\n");
	}
	// Feedback on UCI DOS version
	uii_identify();
    printf(p"%s\n", uii_data);
    cwin_getch();

	return 0;
}
