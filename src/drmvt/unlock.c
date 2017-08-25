#include <bits/ioctl/vt.h>
#include <sys/ioctl.h>

#include <errtag.h>
#include <util.h>

ERRTAG("unlock");
ERRLIST(NEINVAL NENXIO NEPERM NENOENT NEACCES NEIO NEAGAIN);

int main(void)
{
	xchk(sys_ioctli(0, VT_UNLOCKSWITCH, 0), "ioctl", "VT_UNLOCKSWITCH");
	return 0;
}
