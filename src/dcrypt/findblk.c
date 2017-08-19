#include <sys/file.h>
#include <sys/fsnod.h>
#include <sys/symlink.h>

#include <format.h>
#include <fail.h>

#include "config.h"
#include "findblk.h"

ERRTAG = "findblk";

void quit(const char* msg, char* arg, int err)
{
	fail(msg, arg, err);
}

int check_keyindex(int kidx)
{
	return -EINVAL;
}

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
