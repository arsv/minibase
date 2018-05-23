#include <bits/ioctl/vt.h>
#include <sys/ioctl.h>

#include <util.h>
#include <main.h>

ERRTAG("unlock");
ERRLIST(NEINVAL NENXIO NEPERM NENOENT NEACCES NEIO NEAGAIN);

int main(noargs)
{
	int ret;

	if((ret = sys_ioctli(0, VT_UNLOCKSWITCH, 0) < 0))
		fail("ioctl", "VT_UNLOCKSWITCH", ret);

	return 0;
}
