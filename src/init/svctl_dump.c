#include <sys/proc.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "svctl.h"

static int cmp_str(attr at, attr bt, int key)
{
	char* na = uc_sub_str(at, key);
	char* nb = uc_sub_str(bt, key);

	if(!na || !nb)
		return 0;

	return strcmp(na, nb);
}

static int rec_ord(const void* a, const void* b)
{
	attr at = *((attr*)a);
	attr bt = *((attr*)b);
	int ret;

	if((ret = cmp_str(at, bt, ATTR_NAME)))
		return ret;

	return 0;
}

static attr* prep_list(CTX, MSG, int key, qcmp2 cmp)
{
	int n = 0, i = 0;
	attr at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(at->key == key)
			n++;

	attr* refs = heap_alloc(ctx, (n+1)*sizeof(void*));

	for(at = uc_get_0(msg); at && i < n; at = uc_get_n(msg, at))
		if(at->key == key)
			refs[i++] = at;
	refs[i] = NULL;

	qsort(refs, i, sizeof(void*), cmp);

	return refs;
}

static int intlen(int x)
{
	int len = 1;

	if(x < 0) { x = -x; len++; };

	for(; x > 10; len++) x /= 10;

	return len;
}

static int max_pid_len(attr* procs)
{
	int len, max = 0;
	int* pid;

	for(attr* ap = procs; *ap; ap++)
		if((pid = uc_sub_int(*ap, ATTR_PID)))
			if((len = intlen(*pid)) > max)
				max = len;

	return max;
}

static void dump_proc(CTX, AT, int maxpidlen)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;
	char* q;

	char* name = uc_sub_str(at, ATTR_NAME);
	int* pid = uc_sub_int(at, ATTR_PID);

	if(pid)
		q = fmtint(p, e, *pid);
	else
		q = fmtstr(p, e, "-");

	p = fmtpad(p, e, maxpidlen, q);

	if(uc_sub(at, ATTR_RING))
		p = fmtstr(p, e, "*");
	else
		p = fmtstr(p, e, " ");

	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, name ? name : "???");

	*p++ = '\n';

	output(ctx, buf, p - buf);
}

void dump_list(CTX, MSG)
{
	attr* procs = prep_list(ctx, msg, ATTR_PROC, rec_ord);
	int maxlen = max_pid_len(procs);

	for(attr* ap = procs; *ap; ap++)
		dump_proc(ctx, *ap, maxlen);
}

static void newline(CTX)
{
	output(ctx, "\n", 1);
}

static void dump_proc_ring(CTX, MSG)
{
	attr ring;
	int paylen;

	if(!(ring = uc_get(msg, ATTR_RING)))
		return;

	if((paylen = uc_paylen(ring)) <= 0)
		return;

	output(ctx, ring->payload, uc_paylen(ring));

	if(ring->payload[paylen-1] == '\n')
		newline(ctx);
}

void dump_info(CTX, MSG)
{
	char buf[200];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	int* pid = uc_get_int(msg, ATTR_PID);
	char* name = uc_get_str(msg, ATTR_NAME);
	int* exit = uc_get_int(msg, ATTR_EXIT);

	dump_proc_ring(ctx, msg);

	p = fmtstr(p, e, name ? name : "???");

	if(pid) {
		p = fmtstr(p, e, " is running, PID ");
		p = fmtint(p, e, *pid);
	} else if(exit) {
		int status = *exit;

		p = fmtstr(p, e, " has failed");

		if(WIFEXITED(status)) {
			p = fmtstr(p, e, ", exit code ");
			p = fmtint(p, e, WEXITSTATUS(status));
		} else if(WIFSIGNALED(status)) {
			p = fmtstr(p, e, ", killed by signal ");
			p = fmtint(p, e, WTERMSIG(status));
		}
	} else {
		p = fmtstr(p, e, " is not running");
	}

	*p++ = '\n';

	output(ctx, buf, p - buf);
}

void dump_pid(CTX, MSG)
{
	char buf[50];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;
	int* pid;

	if(!(pid = uc_get_int(msg, ATTR_PID)))
		fail("no PID in reply", NULL, 0);

	p = fmtint(p, e, *pid);
	*p++ = '\n';

	output(ctx, buf, p - buf);
}
