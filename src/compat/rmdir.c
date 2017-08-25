#include <sys/fpath.h>

#include <errtag.h>
#include <util.h>

/* This tool is pointless, rm -d should be used instead */

ERRTAG("rmdir");
ERRLIST(NEACCES NEBUSY NEFAULT NEINVAL NELOOP NENOENT NENOMEM NENOTDIR
	NENOTEMPTY NEPERM NEROFS);

int main(int argc, char** argv)
{
	int i;
	long ret;

	if(argc < 2)
		fail("no directories to delete", NULL, 0);
	else for(i = 1; i < argc; i++)
		if((ret = sys_rmdir(argv[i])) < 0)
			fail(NULL, argv[i], ret);

	return 0;
}
