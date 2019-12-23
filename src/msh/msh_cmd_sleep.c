#include <sys/sched.h>

#include <format.h>

#include "msh.h"
#include "msh_cmd.h"

#define NANOFRAC 1000000000 /* nanoseconds in a second */

static void parsetime(CTX, struct timespec* sp, char* str)
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

	return;
err:
	fatal(ctx, "invalid time spec", str);
}

void cmd_sleep(CTX)
{
	char* spec = shift(ctx);

	no_more_arguments(ctx);

	struct timespec sp;

	parsetime(ctx, &sp, spec);

	int ret = sys_nanosleep(&sp, NULL);

	check(ctx, "sleep", NULL, ret);
}
