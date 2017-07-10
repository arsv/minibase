#include <string.h>
#include <util.h>
#include "common.h"
#include "svc.h"

//static int cmp_int(attr at, attr bt, int key)
//{
//	int* na = uc_sub_int(at, key);
//	int* nb = uc_sub_int(bt, key);
//
//	if(!na && nb)
//		return -1;
//	if(na && !nb)
//		return  1;
//	if(!na || !nb)
//		return 0;
//	if(*na < *nb)
//		return -1;
//	if(*na > *nb)
//		return  1;
//
//	return 0;
//}

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
		return -ret;

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

static void dump_attr_list(CTX, attr* list, void (*dump)(CTX, AT))
{
	for(attr* ap = list; *ap; ap++)
		dump(ctx, *ap);
}

static void newline(CTX)
{
	output(ctx, "\n", 1);
}

static void dump_rec(CTX, AT)
{
	output(ctx, "Blah\n", 5);
	newline(ctx);
}

void dump_list(CTX, MSG)
{
	attr* scans = prep_list(ctx, msg, ATTR_PROC, rec_ord);

	init_output(ctx);
	dump_attr_list(ctx, scans, dump_rec);
	fini_output(ctx);
}

void dump_info(CTX, MSG)
{

}

void dump_pid(CTX, MSG)
{

}
