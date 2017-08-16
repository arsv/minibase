#include <fail.h>

#include "config.h"
#include "findblk.h"

ERRTAG = "findblk";

struct bdev bdevs[NBDEVS];
struct part parts[NPARTS];

int nbdevs;
int nparts;

int main(int argc, char** argv)
{
	if(argc > 1)
		fail("too many arguments", NULL, 0);

	load_config();

	open_udev();
	scan_devs();
	wait_udev();

	link_parts();

	return 0;
}
