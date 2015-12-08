#include <bits/fcntl.h>
#include <bits/errno.h>
#include <sys/deletemodule.h>

#include <null.h>
#include <fail.h>

#define TAG "rmmod"

ERRLIST = {
	REPORT(EBUSY), REPORT(EFAULT), REPORT(ENOENT),
	REPORT(EPERM), REPORT(EWOULDBLOCK), RESTASNUMBERS
};

int main(int argc, char** argv)
{
	int flags = O_NONBLOCK;

	if(argc < 2)
		fail(TAG, "module name required", NULL, 0);
	if(argc > 2)
		fail(TAG, "cannot remove several modules", NULL, 0);

	const char* mod = argv[1] + 1;
	switch(argv[1][0]) {
		case '-': flags |=  O_TRUNC; break;
		case '+': flags &= ~O_NONBLOCK; break;
		default: mod--;
	}

	long ret = sysdeletemodule(mod, flags);

	if(ret >= 0) return 0;

	fail(TAG, "cannot remove", mod, -ret);
}
