#include <sys/getcwd.h>
#include <sys/write.h>
#include <fail.h>

#define MAXCWD 2048

ERRTAG = "pwd";
ERRLIST = { REPORT(EINVAL), REPORT(ENOSYS), RESTASNUMBERS };

int main(int argc, char** argv)
{
	char cwd[MAXCWD];
	long ret;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	if((ret = sysgetcwd(cwd, MAXCWD-1)) < 0)
		fail("getcwd", NULL, -ret);
	else if(!ret)
		fail("getcwd: empty return", NULL, 0);

	/* getcwd(2) returns the number of bytes written to the buf,
	   *including* the terminating \0 */
	cwd[ret-1] = '\n';
	syswrite(1, cwd, ret);

	return 0;
}
