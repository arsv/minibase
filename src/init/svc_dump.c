#include <string.h>
#include <format.h>
#include <util.h>
#include "common.h"
#include "svc.h"

static int cmp_str(attr at, attr bt, int key)
{
	char* na = uc_sub_str(at, key);
	char* nb = uc_sub_str(bt, key);

	if(!na || !nb)
		return 0;

	return strcmp(na, nb);
}

static int rec_ord(const void* a, const void* b, long p)
{
	attr at = *((attr*)a);
	attr bt = *((attr*)b);
	int ret;

	if((ret = cmp_str(at, bt, ATTR_NAME)))
		return ret;

	return 0;
}

static attr* prep_list(CTX, MSG, int key, qcmp cmp)
{
	int n = 0, i = 0;
	attr at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(at->key == key)
			n++;

	attr* refs = halloc(&ctx->hp, (n+1)*sizeof(void*));

	for(at = uc_get_0(msg); at && i < n; at = uc_get_n(msg, at))
		if(at->key == key)
			refs[i++] = at;
	refs[i] = NULL;

	qsort(refs, i, sizeof(void*), cmp, 0);

	return refs;
}

static int max_proc_len(attr* procs)
{
	int len, max = 0;
	char* name;

	for(attr* ap = procs; *ap; ap++)
		if((name = uc_sub_str(*ap, ATTR_NAME)))
			if((len = strlen(name)) > max)
				max = len;

	return len;
}

static void dump_proc(CTX, AT, int maxlen)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	char* name = uc_sub_str(at, ATTR_NAME);
	int* pid = uc_sub_int(at, ATTR_PID);

	if(pid)
		p = fmtint(p, e, *pid);
	else
		p = fmtstr(p, e, "-");

	if(uc_sub(at, ATTR_RING))
		p = fmtstr(p, e, "*");
	else
		p = fmtstr(p, e, " ");

	p = fmtstr(p, e, " ");
	p = fmtpadr(p, e, maxlen, fmtstr(p, e, name ? name : "???"));

	*p++ = '\n';

	output(ctx, buf, p - buf);
}

void dump_msg(CTX, MSG)
{
	uc_dump(msg);
}

void dump_list(CTX, MSG)
{
	attr* procs = prep_list(ctx, msg, ATTR_PROC, rec_ord);
	int maxlen = max_proc_len(procs);

	init_output(ctx);

	for(attr* ap = procs; *ap; ap++)
		dump_proc(ctx, *ap, maxlen);

	fini_output(ctx);
}

void dump_info(CTX, MSG)
{
	uc_dump(msg);
}

void dump_pid(CTX, MSG)
{
	uc_dump(msg);
}
