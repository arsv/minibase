#include <bits/errno.h>
#include <sys/rmdir.h>

#include <fail.h>
#include <null.h>

static const char* TAG = "rmdir";

ERRLIST = {
	REPORT(EACCES), REPORT(EBUSY), REPORT(EFAULT), REPORT(EINVAL),
	REPORT(ELOOP), REPORT(ENOENT), REPORT(ENOMEM), REPORT(ENOTDIR),
	REPORT(ENOTEMPTY), REPORT(EPERM), REPORT(EROFS), RESTASNUMBERS
};

int main(int argc, char** argv)
{
	int i;
	long ret;

	if(argc < 2)
		fail(TAG, "no directories to delete", NULL, 0);
	else for(i = 1; i < argc; i++)
		if((ret = sysrmdir(argv[i])) < 0)
			fail(TAG, NULL, argv[i], -ret);

	return 0;
}
