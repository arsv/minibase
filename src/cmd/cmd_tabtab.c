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

static void reset_dirlist(TT)
{
	memzero(tt, sizeof(*tt));
}

static int init_buffer(TT)
{
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

static int gotprefix(struct dirent* de, char* pref, int plen)
{
	char* name = de->name;
	int nlen = strlen(de->name);

	if(!pref)
		return 1;
	else if(nlen < plen)
		return 0;
	else if(strncmp(name, pref, plen))
		return 0;

	return 1;
}

static int executable(int at, struct dirent* de)
{
	char* name = de->name;
	struct stat st;
	int flags = AT_NO_AUTOMOUNT;

	if((sys_fstatat(at, name, &st, flags)) < 0)
		return 0;

	if((st.mode & S_IFMT) != S_IFREG)
		return 0;
	if(!(st.mode & 0111))
		return 0;

	return 1;
}

static void save_name(TT, struct dirent* de)
{
	char* name = de->name;
	int nlen = strlen(name);

	struct fname* fn;
	int total = sizeof(*fn) + nlen + 1;

	if(!(fn = alloc_mem(tt, total)))
		return;

	fn->len = total;
	fn->isdir = (de->type == DT_DIR);
	memcpy(fn->name, name, nlen+1);

	tt->count++;
}

static int scan_directory(TT, char* dir, char* pref, int plen, int exe)
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

			if(dotddot(de->name))
				continue;
			if(plen && *pref == '.')
				;
			else if(de->name[0] == '.')
				continue;

			if(!gotprefix(de, pref, plen))
				continue;
			if(exe && !executable(fd, de))
				continue;

			save_name(tt, de);
		}
	}
out:
	sys_close(fd);

	return ret;
}

static int prep_dirlist(TT, char* dir, char* pref, int plen, int exe)
{
	int ret;

	reset_dirlist(tt);

	if((ret = scan_directory(tt, dir, pref, plen, exe)) < 0)
		return ret;

	index_entries(tt);

	return 0;
}

void list_cwd(CTX)
{
	struct tabtab* tt = &ctx->tts;

	if(prep_dirlist(tt, ".", NULL, 0, 0))
		return;

	dump_dirlist(tt, ctx->cols);

	free_dirlist(tt);
}

static void try_path_dir(TT, XA, char* dir, int dlen)
{
	char buf[dlen + 1];

	memcpy(buf, dir, dlen);
	buf[dlen] = '\0';

	scan_directory(tt, buf, xa->base, xa->blen, 1);
}

static void complete_command(CTX, TT, XA)
{
	char* path;

	if(!(path = getenv(ctx->envp, "PATH")))
		return;

	char* p = path;
	char* e = path + strlen(path);

	while(p < e) {
		char* q = strecbrk(p, e, ':');

		if(q > p)
			try_path_dir(tt, xa, p, q - p);

		p = q + 1;
	}
}

static void complete_filename(TT, XA)
{
	prep_dirlist(tt, xa->dir, xa->base, xa->blen, xa->initial);
}

/* When completing "fi" yields a single result, [ "file" ], insert
   the missing part and add a space to indicate end of argument. */

static void insert_single(CTX, TT, XA)
{
	int* index = tt->buf + tt->idx;
	struct fname* fn = tt->buf + index[0];
	char* name = fn->name;
	int nlen = strlen(name);

	int plen = xa->blen;
	char quote = xa->quote;

	if(nlen < plen) /* the name is shorter than typed prefix */
		return; /* should never happen */

	insert(ctx, name + plen, nlen - plen); /* add the missing part */

	if(fn->isdir) {
		insert(ctx, "/", 1);
	} else {
		if(quote) /* when completing |"filen|, close the quote |ame"| */
			insert(ctx, &quote, 1);
		insert(ctx, " ", 1);
	}
}

/* comm="filename" name="filled" -> 3 "fil" */

static int maxprefix(char* comm, int clen, char* name, int nlen)
{
	int i;

	if(nlen < clen)
		clen = nlen;

	for(i = 0; i < clen; i++)
		if(comm[i] != name[i])
			break;

	return i;
}

/* When completing "fi" yields [ "file-a", "file-b", "file-c" ],
   insert the common part "le-" immediately. */

static void maybe_insert_common(CTX, TT, XA)
{
	void* buf = tt->buf;
	int i, count = tt->count; /* always >1 here */
	int* index = buf + tt->idx;
	int plen = xa->blen;

	struct fname* fn = tt->buf + index[0];
	char* comm = fn->name;        /* common prefix  */
	int clen = strlen(fn->name);  /* and its length */

	for(i = 1; i < count; i++) {
		fn = buf + index[i];

		char* name = fn->name;
		int nlen = strlen(name);

		clen = maxprefix(comm, clen, name, nlen);
	}

	if(clen <= plen)
		return;

	insert(ctx, comm + plen, clen - plen);
}

void single_tab(CTX)
{
	struct exparg xa;
	struct tabtab* tt = &ctx->tts;

	if(expand_arg(ctx, &xa))
		return;

	reset_dirlist(tt);

	if(xa.noslash && xa.initial)
		complete_command(ctx, tt, &xa);
	else
		complete_filename(tt, &xa);

	index_entries(tt);

	int count = tt->count;

	if(count == 1)
		insert_single(ctx, tt, &xa);
	else if(count > 1)
		maybe_insert_common(ctx, tt, &xa);

	if(count >= 2)
		ctx->tab = 1;
	else
		free_dirlist(tt);

	free_exparg(&xa);
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
