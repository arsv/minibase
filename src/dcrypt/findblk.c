#include <sys/file.h>
#include <sys/fpath.h>

#include <format.h>
#include <errtag.h>
#include <util.h>

#include "common.h"
#include "findblk.h"

ERRTAG("findblk");

void quit(const char* msg, char* arg, int err)
{
	fail(msg, arg, err);
}

int check_keyindex(int kidx)
{
	(void)kidx;
	return -EINVAL;
}

int main(int argc, char** argv)
{
	(void)argv;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	load_config();

	open_udev();
	scan_devs();
	wait_udev();

	link_parts();

	return 0;
}
