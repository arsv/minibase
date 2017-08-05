#include <bits/ioctl/vt.h>
#include <sys/ioctl.h>
#include <fail.h>

ERRTAG = "unlock";
ERRLIST = {
	REPORT(EINVAL), REPORT(ENXIO), REPORT(EPERM), REPORT(ENOENT),
	REPORT(EACCES), REPORT(EIO), REPORT(EAGAIN), RESTASNUMBERS
};

int main(void)
{
	xchk(sys_ioctli(0, VT_UNLOCKSWITCH, 0), "ioctl", "VT_UNLOCKSWITCH");
	return 0;
}
