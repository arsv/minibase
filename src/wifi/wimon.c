#include <bits/errno.h>
#include <sys/unlink.h>
#include <netlink.h>

#include <fail.h>

#include "config.h"
#include "wimon.h"

ERRTAG = "wimon";
ERRLIST = {
	REPORT(ENOMEM), REPORT(EINVAL), REPORT(ENOBUFS), REPORT(EFAULT),
	REPORT(EINTR), REPORT(ENOENT), RESTASNUMBERS
};

char** environ;

int main(int argc, char** argv, char** envp)
{
	environ = envp;

	setup_rtnl();
	setup_genl();
	setup_ctrl();

	setup_signals();
	setup_pollfds();

	mainloop();

	unlink_ctrl();

	return 0;
}
