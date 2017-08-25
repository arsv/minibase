#include <sys/file.h>
#include <sys/dents.h>
#include <sys/ioctl.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <heap.h>

#include "inputs.h"

static void read_whole_dir(struct heap* hp, int fd, const char* dir)
{
	long ret;

	while((ret = sys_getdents(fd, hp->ptr, hp->end - hp->ptr)) > 0) {
		hp->ptr += ret;
		hextend(hp, PAGE);
	} if(ret < 0)
		fail("cannot read entries from", dir, ret);
}

static int cmp_de_by_name(const void* a, const void* b, long _)
{
	struct dirent* da = *((struct dirent**)a);
	struct dirent* db = *((struct dirent**)b);

	return natcmp(da->name, db->name);
}

static int de_reclen(void* p)
{
	return ((struct dirent*)p)->reclen;
}

static int de_type(void* p)
{
	return ((struct dirent*)p)->type;
}

static struct dirent** index_dents(struct heap* hp, void* dents, void* deend)
{
	void* p;
	int i = 0, nument = 0;

	for(p = dents; p < deend; p += de_reclen(p))
		if(de_type(p) == DT_CHR)
			nument++;

	int len = (nument + 1) * sizeof(struct dirent*);
	struct dirent** idx = halloc(hp, len);

	for(p = dents; p < deend; p += de_reclen(p))
		if(de_type(p) == DT_CHR)
			idx[i++] = p;
	idx[i] = NULL;

	qsort(idx, nument, sizeof(*idx), cmp_de_by_name, 0);
	
	return idx;
}

void forall_inputs(const char* dir, dumper f)
{
	int fd;
	struct heap hp;

	if((fd = sys_open(dir, O_RDONLY | O_DIRECTORY)) < 0)
		fail("open", dir, fd);

	hinit(&hp, PAGE);

	void* dents = hp.ptr;
	read_whole_dir(&hp, fd, dir);
	void* deend = hp.ptr;

	struct dirent** idx = index_dents(&hp, dents, deend);
	struct dirent** dep;

	for(dep = idx; *dep; dep++)
		with_entry(dir, (*dep)->name, f);

}
