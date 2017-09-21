#include <bits/null.h>
#include <sys/sched.h>

#include <format.h>

#include "msh.h"
#include "msh_cmd.h"

#define NANOFRAC 1000000000 /* nanoseconds in a second */

static int parsetime(CTX, struct timespec* sp, char* str)
{
	unsigned long sec = 0;
	unsigned long nsec = 0;
	unsigned long nmul = 0;
	char *p, *q;

	if(!*str)
		goto err;
	if(!(p = parseulong(str, &sec)))
		goto err;
	if(!*p)
		goto out;
	if(*p++ != '.')
		goto err;
	if(!(q = parseulong(p, &nsec)) || *q)
		goto err;

	for(nmul = NANOFRAC; p < q; p++)
		nmul /= 10;
out:
	sp->sec = sec;
	sp->nsec = nsec*nmul;

	return 0;
err:
	return error(ctx, "invalid time spec", str, 0);
}

int cmd_sleep(CTX)
{
	struct timespec sp;
	int ret;

	if(noneleft(ctx))
		return -1;
	if((ret = parsetime(ctx, &sp, shift(ctx))))
		return ret;
	if(moreleft(ctx))
		return -1;
	if((ret = sys_nanosleep(&sp, NULL)))
		return error(ctx, "sleep", NULL, ret);

	return 0;
}
