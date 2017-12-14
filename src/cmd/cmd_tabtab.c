#include <sys/file.h>
#include <sys/mman.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <printf.h>
#include <util.h>

#include "cmd.h"
#include "unicode.h"

/* Tab completition, filenames only. On the first keypress, relevant
   directories are scanned and full basenames of possible options are
   stored in struct tabtab, sorted and indexed. The second Tab dumps
   the stored index.

   The mmaped block in TT may need to grow. To avoid the hassle of
   keeping pointers valid across mremap, only offsets are stored.

           i0  i1  i2  i3      ic
           v   v   v   v       v
    buf -> FN0 FN1 FN2 FN3 ... FNc i0 i1 ... ic          FN = struct fname
                                   ^ idx       ^ptr      c = (count-1)

   The index is int[count] containg offsets of individusl struct fname's. */

#define TT struct tabtab* tt

struct fname {
	ushort len;
	char isdir;
	char name[];
};

static int max_entry_viswi(TT)
{
	int i, n = tt->count;
	int* index = (int*)(tt->buf + tt->idx);
	int entvi, maxvi = 0;

	for(i = 0; i < n; i++) {
		struct fname* fn = (tt->buf + index[i]);

		char* name = fn->name;
		int nlen = fn->len - sizeof(*fn) - 1;

		entvi = visual_width(name, nlen);

		if(fn->isdir)
			entvi++;

		if(entvi > maxvi)
			maxvi = entvi;
	}

	return maxvi;
}

static void dump_dirlist(TT, int cols)
{
	int i, n = tt->count;
	int* index = (int*)(tt->buf + tt->idx);
	char outbuf[2048];

	struct bufout bo = {
		.fd = STDOUT,
		.len = sizeof(outbuf),
		.ptr = 0,
		.buf = outbuf
	};

	int ts = max_entry_viswi(tt) + 2;

	int viswi = 0;
	char pad[ts];

	memset(pad, ' ', ts);

	for(i = 0; i < n; i++) {
		struct fname* fn = (tt->buf + index[i]);

		FMTBUF(p, e, out, fn->len);
		p = fmtstr(p, e, fn->name);
		if(fn->isdir) p = fmtstr(p, e, "/");
		FMTEND(p, e);

		int fnvw = visual_width(out, p - out);
		int lead = viswi ? 2 : 0;

		lead += (ts - ((viswi + lead) % ts)) % ts;

		if(viswi + lead + fnvw + 2 < cols) {
			viswi += lead + fnvw;
			bufout(&bo, pad, lead);
		} else {
			viswi = fnvw;
			bufout(&bo, "\n", 1);
		}
		bufout(&bo, out, p - out);
	}

	if(i) bufout(&bo, "\n", 1);

	bufoutflush(&bo);
}

static int init_buffer(TT)
{
	tt->count = 0;
	tt->ptr = 0;

	if(tt->buf)
		return 0;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	void* buf = sys_mmap(NULL, PAGE, prot, flags, -1, 0);

	if(mmap_error(buf))
		return (long)buf;

	tt->buf = buf;
	tt->size = PAGE;

	return 0;
}

static int alloc_ptr(TT, int size)
{
	int ret = tt->ptr;

	if(ret + size > tt->size) {
		int newsize = pagealign(tt->ptr + size);

		int flags = MREMAP_MAYMOVE;
		void* buf = sys_mremap(tt->buf, tt->size, newsize, flags);

		if(mmap_error(buf))
			return -1;

		tt->buf = buf;
		tt->size = newsize;
	}

	tt->ptr += size;

	return ret;
}

static void* alloc_mem(TT, int size)
{
	int ptr;

	if((ptr = alloc_ptr(tt, size)) < 0)
		return NULL;

	return tt->buf + ptr;
}

static int cmpent(const void* ap, const void* bp, long ttp)
{
	int av = *((int*)ap);
	int bv = *((int*)bp);
	struct tabtab* tt = (struct tabtab*) ttp;

	struct fname* fa = (tt->buf + av);
	struct fname* fb = (tt->buf + bv);

	if(fa->isdir && !fb->isdir)
		return -1;
	if(!fa->isdir && fb->isdir)
		return 1;

	return natcmp(fa->name, fb->name);
}

static void index_entries(TT)
{
	void* buf;
	int count = tt->count;

	if(!(buf = alloc_mem(tt, count*sizeof(int)))) {
		tt->idx = 0;
		return;
	}

	int* index = buf;
	int i = 0;

	tt->idx = (int)(buf - tt->buf);

	int ptr = 0;
	int end = tt->ptr;

	while(ptr < end) {
		struct fname* fn = tt->buf + ptr;

		if(i >= count)
			break;

		index[i++] = ptr;

		ptr += fn->len;
	}

	qsortx(index, count, sizeof(int), cmpent, (long)tt);
}

static void free_dirlist(TT)
{
	if(!tt->buf) return;

	sys_munmap(tt->buf, tt->size);

	memzero(tt, sizeof(*tt));
}

static void check_dent(TT, struct dirent* de, char* pref, int plen)
{
	char* name = de->name;
	int nlen = strlen(de->name);

	if(!pref)
		;
	else if(nlen < plen)
		return;
	else if(strncmp(name, pref, plen))
		return;

	struct fname* fn;
	int total = sizeof(*fn) + nlen + 1;

	if(!(fn = alloc_mem(tt, total)))
		return;

	fn->len = total;
	fn->isdir = (de->type == DT_DIR);
	memcpy(fn->name, name, nlen+1);

	tt->count++;
}

static int prep_dirlist(TT, char* dir, char* pref, int plen)
{
	int fd, rd, ret;
	char dirbuf[2048];

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		return fd;
	if((ret = init_buffer(tt)) < 0)
		goto out;

	while((rd = sys_getdents(fd, dirbuf, sizeof(dirbuf))) > 0) {
		void* ptr = dirbuf;
		void* end = dirbuf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;

			if(de->reclen <= 0)
				break;

			ptr += de->reclen;

			if(de->name[0] == '.')
				continue;

			check_dent(tt, de, pref, plen);
		}
	}

	index_entries(tt);
out:
	sys_close(fd);

	return ret;
}

void list_cwd(CTX)
{
	struct tabtab* tt = &ctx->tts;

	if(prep_dirlist(tt, ".", NULL, 0))
		return;

	dump_dirlist(tt, ctx->cols);

	free_dirlist(tt);
}

void single_tab(CTX)
{

}

void double_tab(CTX)
{
	struct tabtab* tt = &ctx->tts;

	if(!tt->buf)
		return;

	dump_dirlist(tt, ctx->cols);
}

void cancel_tab(CTX)
{
	ctx->tab = 0;

	free_dirlist(&ctx->tts);
}
