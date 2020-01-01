#include <sys/sched.h>

#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct sht {
	char name[8];
	int idx;
} shtypes[] = {
	{ "normal", SCHED_NORMAL },
	{ "other",  SCHED_NORMAL },
	{ "fifo",   SCHED_FIFO   },
	{ "rr",     SCHED_RR     },
	{ "round",  SCHED_RR     },
	{ "batch",  SCHED_BATCH  },
	{ "idle",   SCHED_IDLE   }
};

static int resolve_sched(CTX, char* type)
{
	const struct sht* sh;

	for(sh = shtypes; sh < ARRAY_END(shtypes); sh++)
		if(!strncmp(sh->name, type, sizeof(sh->name)))
			return sh->idx;

	fatal(ctx, "unknown priority", type);
}

void cmd_setprio(CTX)
{
	int prio;

	shift_int(ctx, &prio);
	no_more_arguments(ctx);

	int ret = sys_setpriority(0, 0, prio);

	check(ctx, "setpriority", NULL, ret);
}

void cmd_setcpus(CTX)
{
	int id;
	struct cpuset mask;

	memzero(&mask, sizeof(mask));
next:
	shift_int(ctx, &id);
	cpuset_set(&mask, id);

	if(got_more_arguments(ctx))
		goto next;

	int ret = sys_sched_setaffinity(0, &mask);

	check(ctx, "setaffinity", NULL, ret);
}

void cmd_scheduler(CTX)
{
	char* type = shift(ctx);
	struct sched_param p;

	memzero(&p, sizeof(p));

	if(got_more_arguments(ctx))
		shift_int(ctx, &p.priority);

	no_more_arguments(ctx);

	int tidx = resolve_sched(ctx, type);

	int ret = sys_setscheduler(0, tidx, &p);

	check(ctx, "setscheduler", NULL, ret);
}
