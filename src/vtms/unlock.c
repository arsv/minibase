#include <bits/ioctl/vt.h>
#include <sys/ioctl.h>

#include <errtag.h>
#include <util.h>

ERRTAG("unlock");
ERRLIST(NEINVAL NENXIO NEPERM NENOENT NEACCES NEIO NEAGAIN);

int main(void)
{
	int ret;

	if((ret = sys_ioctli(0, VT_UNLOCKSWITCH, 0) < 0))
		fail("ioctl", "VT_UNLOCKSWITCH", ret);

	return 0;
}
