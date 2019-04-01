#include <sys/file.h>
#include <sys/time.h>
#include <sys/timex.h>

#include <string.h>
#include <dirs.h>
#include <util.h>

#include "timed.h"

void init_clock_state(CTX)
{
	struct timex tmx;
	int ret;

	memzero(&tmx, sizeof(tmx));

	tmx.modes = ADJ_STATUS;
	tmx.status = STA_UNSYNC;

	if((ret = sys_adjtimex(&tmx)) < 0)
		quit("adjtimex", NULL, ret);
}

static int slew_time_constant(int ival)
{
	int tc = 0;

	while(ival > 1) {
		ival >>= 1;
		tc++;
	}

	return tc > 4 ? tc - 5 : 0;
}

static void reset_interval(CTX)
{
	ctx->interval = MIN_POLL_INTERVAL;
}

static void increase_interval(CTX)
{
	if(!ctx->interval)
		ctx->interval = MIN_POLL_INTERVAL;
	else if(ctx->interval < MAX_POLL_INTERVAL)
		ctx->interval *= 2;
}

static void decrease_interval(CTX)
{
	if(!ctx->interval)
		ctx->interval = MIN_POLL_INTERVAL;
	else if(ctx->interval > MIN_POLL_INTERVAL)
		ctx->interval /= 2;
}

static void request_clock_slew(struct timex* tmx, int64_t dt, int ival)
{
	int64_t ns = (dt * 1000000000 / (1LL<<32));

	tmx->modes = ADJ_OFFSET | ADJ_NANO | ADJ_STATUS | ADJ_TIMECONST;
	tmx->status = 0;
	tmx->offset = ns;
	tmx->constant = slew_time_constant(ival);
}

static void request_clock_step(struct timex* tmx, int64_t dt)
{
	int64_t ds = dt >> 32;
	uint64_t qs = dt & 0xFFFFFFFF;
	uint64_t ns = qs * 1000000000 / (1ULL<<32);

	tmx->modes = ADJ_SETOFFSET | ADJ_NANO | ADJ_STATUS;
	tmx->time.sec = ds;
	tmx->time.usec = ns;
}

void consider_synching(CTX)
{
	struct timex tmx;
	struct serv* sv = current(ctx);
	int ret;

	int64_t lo = ctx->lo;
	int64_t hi = ctx->hi;
	int64_t over = sv->rtt / 2;
	int64_t dt;

	if(lo > 0)                  /* Do the least possible correction, */
		dt = -(lo + over);  /* with a bit of overlap.            */
	else if(hi < 0)
		dt = -(hi - over);
	else
		return increase_interval(ctx);

	int64_t ad = dt < 0 ? -dt : dt; /* abs(dt) */

	if((ad < (1<<22))) /* ~1ms */
		return increase_interval(ctx);

	ctx->syncdt = dt;
	ctx->synctime = ctx->polltime;

	if(ad >= (1<<31)) /* 500ms, step threshold */
		reset_interval(ctx);
	else if(ad >= (1<<29)) /* 125ms */
		decrease_interval(ctx);
	else
		increase_interval(ctx);

	memzero(&tmx, sizeof(tmx));

	if(ad >= (1LL<<31))
		request_clock_step(&tmx, dt);
	else
		request_clock_slew(&tmx, dt, ctx->interval);

	if((ret = sys_adjtimex(&tmx)) < 0)
		warn("adjtimex", NULL, ret);
}
