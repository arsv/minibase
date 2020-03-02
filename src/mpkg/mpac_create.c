#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/splice.h>

#include <string.h>
#include <format.h>
#include <main.h>
#include <util.h>

#include "mpac.h"

/* The code here creates an archive from a source directory,
   preserving its internal structure (subdirs etc).

   This is done in two steps: first, we scan the directory, whole,
   stat and sort the entries, and write the index. Then, we scan
   the directory second time, now following the index, and append
   individual file content to the achive. */

struct ent {
	short len;
	short type; /* one of TAG_DIR, TAG_FILE, TAG_EXEC, TAG_LINK */
	uint size;
	char name[];
};

static int cmp_ent(void* pa, void* pb)
{
	struct ent* a = pa;
	struct ent* b = pb;

	int da = (a->type == TAG_DIR);
	int db = (b->type == TAG_DIR);

	if(!da && db)
		return -1;
	if(da && !db)
		return 1;

	return strcmp(a->name, b->name);
}

static struct ent** index_dir(CTX, void* p0, void* p1)
{
	int i = 0, count = 0;

	void* p = p0;
	void* e = p1;

	while(p < e) {
		struct ent* et = p;
		int len = et->len;

		if(len <= 0) break;

		p += len;
		count++;
	}

	void* q = p0;

	int size = (count+1)*sizeof(struct ent*);
	struct ent** idx = heap_alloc(ctx, size);

	while(q < e) {
		struct ent* et = q;
		int len = et->len;

		if(len <= 0) break;
		if(i >= count) break;

		q += len;
		idx[i++] = et;
	}

	qsortp(idx, i, cmp_ent);

	idx[i] = NULL;

	return idx;
}

static void update_header_size(CTX, int mode, int nlen, uint size)
{
	int hdrsize = ctx->hsize;

	hdrsize += nlen + 2;

	if((mode & S_IFMT) != S_IFDIR)
		hdrsize += 4;

	if(hdrsize < 0)
		fail("out of header space", NULL, 0);

	ctx->hsize = hdrsize;
}

static void check_dent(CTX, int at, char* name)
{
	struct stat st;
	int flags = AT_SYMLINK_NOFOLLOW;
	int ret;

	if((ret = sys_fstatat(at, name, &st, flags)) < 0)
		failx(ctx, "stat", name, ret);

	struct ent* ep;
	int nlen = strlen(name);
	int mode = st.mode;
	int need = sizeof(*ep) + nlen + 1;
	int type = mode & S_IFMT;
	uint size;

	if(type == S_IFDIR) {
		size = 0;
		type = TAG_DIR;
	} else if(type == S_IFLNK) {
		if(st.size > 0xFFFF)
			fail(NULL, name, -E2BIG);
		size = st.size;
		type = TAG_LINK;
	} else if(type == S_IFREG) {
		if(st.size > 0xFFFFFFFF)
			fail(NULL, name, -E2BIG);
		size = st.size;
		type = (mode & 0111) ? TAG_EXEC : TAG_FILE;
	} else {
		failx(ctx, "special file:", name, 0);
	}

	need = (need + 3) & ~3;

	ep = heap_alloc(ctx, need);

	ep->len = need;
	ep->type = type;
	ep->size = size;

	memcpy(ep->name, name, nlen + 1);

	update_header_size(ctx, mode, nlen, size);
};

static uint last_idx_offset(CTX)
{
	void* brk = ctx->brk;
	void* idx = ctx->idx;
	void* ptr = ctx->ptr;

	if(idx < brk || idx >= ptr)
		fail("index ptr not in heap", NULL, 0);

	uint off = idx - brk;

	if((brk + off != idx))
		fail("non-representable index offset", NULL, 0);

	return off;
}

static void scan_directory(CTX);

static void enter_dir(CTX, char* name)
{
	int at = ctx->at;
	int depth = ctx->depth;
	int fd, ret;

	if(depth >= MAXDEPTH)
		failx(ctx, NULL, name, -ELOOP);

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		failx(ctx, NULL, name, fd);

	ctx->path[depth] = name;
	ctx->pfds[depth] = at;
	ctx->depth = depth + 1;
	ctx->at = fd;

	scan_directory(ctx);

	if((ret = sys_close(ctx->at)) < 0)
		fail("close", NULL, ret);

	depth = ctx->depth - 1;
	ctx->at = ctx->pfds[depth];
	ctx->depth = depth;
}

static void process_dir(CTX, struct ent** idx)
{
	struct ent *ep, **epp;

	for(epp = idx; (ep = *epp); epp++) {
		int type = ep->type;

		if(type != TAG_DIR) continue;

		enter_dir(ctx, ep->name);

		ep->size = last_idx_offset(ctx);
	}

	ctx->idx = idx;
}

static void scan_directory(CTX)
{
	int fd = ctx->at;
	void* buf = ctx->dirbuf;
	int len = ctx->dirlen;
	int ret;

	void* p0 = ctx->ptr;

	while((ret = sys_getdents(fd, buf, len)) > 0) {
		void* ptr = buf;
		void* end = buf + ret;

		while(ptr < end) {
			struct dirent* de = ptr;

			char* name = de->name;
			int reclen = de->reclen;

			if(reclen <= 0)
				break;

			ptr += reclen;

			if(dotddot(name))
				continue;

			check_dent(ctx, fd, name);
		}
	} if(ret < 0) {
		failx(ctx, NULL, "", ret);
	};

	void* p1 = ctx->ptr;

	struct ent** idx = index_dir(ctx, p0, p1);

	process_dir(ctx, idx);
}

static void scan_files(CTX, char* start)
{
	int fd;

	if((fd = sys_open(start, O_DIRECTORY)) < 0)
		fail(NULL, start, fd);

	ctx->root = start;
	ctx->at = fd;

	scan_directory(ctx);
}

static void append(CTX, void* buf, int len)
{
	void* dst = heap_alloc(ctx, len);

	memcpy(dst, buf, len);
}

static void put(CTX, int c)
{
	byte* dst = heap_alloc(ctx, 1);

	*dst = (c & 0xFF);
}

static void put_entries(CTX, struct ent** idx, int depth);

static void put_dir(CTX, struct ent* p, int depth)
{
	char* name = p->name;
	int nlen = strlen(name);
	int size = p->size;

	put(ctx, TAG_DIR | depth);

	append(ctx, name, nlen + 1);

	if(!size) fail("invalid dir ref in", p->name, 0);

	struct ent** next = ctx->brk + size;

	put_entries(ctx, next, depth + 1);
}

static void put_link(CTX, struct ent* p)
{
	char* name = p->name;
	int nlen = strlen(name);
	int size = p->size;
	int type = TAG_LINK;

	if(size <= 0xFF) {
		put(ctx, type |= 0);
		put(ctx, size);
	} else if(size <= 0xFFFF) {
		put(ctx, type |= 1);
		put(ctx, size >> 8);
		put(ctx, size);
	} else {
		fail(NULL, name, -E2BIG);
	}

	append(ctx, name, nlen + 1);
}

static void put_file(CTX, struct ent* p)
{
	char* name = p->name;
	int nlen = strlen(name);
	int type = p->type; /* only TAG_FILE, TAG_EXEC or TAG_LINK here */
	int size = p->size;

	if(size <= 0xFF) {
		put(ctx, type |= 0);
		put(ctx, size);
	} else if(size <= 0xFFFF) {
		put(ctx, type |= 1);
		put(ctx, size);
		put(ctx, size >> 8);
	} else if(size <= 0xFFFFFF) {
		put(ctx, type |= 2);
		put(ctx, size);
		put(ctx, size >> 8);
		put(ctx, size >> 16);
	} else {
		put(ctx, type |= 3);
		put(ctx, size);
		put(ctx, size >> 8);
		put(ctx, size >> 16);
		put(ctx, size >> 24);
	}

	append(ctx, name, nlen + 1);
}

static void put_entries(CTX, struct ent** idx, int depth)
{
	struct ent *p, **pp;

	if(!idx) return;

	for(pp = idx; (p = *pp); pp++) {
		int type = p->type;

		if(type == TAG_DIR)
			put_dir(ctx, p, depth);
		else if(type == TAG_LINK)
			put_link(ctx, p);
		else
			put_file(ctx, p);
	}
}

static void dump_symlink(CTX, struct ent* p)
{
	int at = ctx->at;
	char* name = p->name;
	uint size = p->size;
	int ret, fd = ctx->fd;

	void* buf = ctx->dirbuf;
	int len = ctx->dirlen;

	if((ret = sys_readlinkat(at, name, buf, len)) < 0)
		fail(NULL, name, ret);

	if(ret != size)
		fail("size mismatch in", name, 0);

	if((ret = writeall(fd, buf, ret)) < 0)
		fail("write", NULL, ret);
}

static void dump_bindata(CTX, struct ent* p)
{
	int at = ctx->at;
	char* name = p->name;
	uint size = p->size;
	int fd, ret;
	int out = ctx->fd;

	if((fd = sys_openat(at, name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	if((ret = sys_sendfile(out, fd, NULL, size)) < 0)
		failx(ctx, "sendfile", name, ret);
	if(ret != size)
		failx(ctx, NULL, name, -EINTR);

	sys_close(fd);
}

static void dump_content(CTX, struct ent** idx);

static void dump_nextdir(CTX, struct ent* p)
{
	int at = ctx->at;
	int depth = ctx->depth;
	int fd, ret;

	uint size = p->size;
	char* name = p->name;
	struct ent** nextidx = ctx->brk + size;

	if(depth >= MAXDEPTH)
		fail("tree depth exceeded", NULL, 0);

	if((fd = sys_openat(at, name, O_DIRECTORY | O_PATH)) < 0)
		failx(ctx, NULL, name, fd);

	ctx->path[depth] = name;
	ctx->pfds[depth] = at;
	ctx->at = fd;
	ctx->depth = depth + 1;

	dump_content(ctx, nextidx);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);

	ctx->at = at;
	ctx->depth = depth;
}

static void dump_content(CTX, struct ent** idx)
{
	struct ent *p, **pp;

	if(!idx) return;

	for(pp = idx; (p = *pp); pp++) {
		int type = p->type;

		if(type == TAG_DIR)
			dump_nextdir(ctx, p);
		else if(type == TAG_LINK)
			dump_symlink(ctx, p);
		else
			dump_bindata(ctx, p);
	}
}

static void* put_file_tag(CTX, void* ptr, int size)
{
	byte tag[8];

	memcpy(tag, "PAC", 3);

	byte* p = tag + 3;

	if(size <= 0xFF) {
		*p++ = '@';
		*p++ = (size >> 0);
	} else if(size <= 0xFFFF) {
		*p++ = 'A';
		*p++ = (size >> 0);
		*p++ = (size >> 8);
	} else if(size <= 0xFFFFFF) {
		*p++ = 'B';
		*p++ = (size >> 0);
		*p++ = (size >> 8);
		*p++ = (size >> 16);
	} else if(size <= 0x7FFFFFFF) {
		*p++ = 'C';
		*p++ = (size >> 0);
		*p++ = (size >> 8);
		*p++ = (size >> 16);
		*p++ = (size >> 24);
	} else {
		fail("out of header space", NULL, 0);
	}

	int n = p - tag;
	int s = 8 - n;

	memcpy(ptr + s, tag, n);

	return ptr + s;
}

static void open_output(CTX, char* out)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;

	if((fd = sys_open3(out, flags, mode)) < 0)
		fail(NULL, out, fd);

	ctx->fd = fd;
}

static void flush_header(CTX, char* out, void* hdr, uint len)
{
	int ret, fd = ctx->fd;

	if((ret = writeall(fd, hdr, len)) < 0)
		fail(NULL, out, ret);
}

static void dump_packed(CTX, char* out)
{
	int need = ctx->hsize + 8;
	void* ptr = ctx->ptr;

	open_output(ctx, out);

	(void)heap_alloc(ctx, need);

	void* entries = ptr + 8;

	ctx->ptr = ptr + 8;

	put_entries(ctx, ctx->idx, 0);

	void* entend = ctx->ptr;
	uint entsize = entend - entries;

	ptr = put_file_tag(ctx, ptr, entsize);

	flush_header(ctx, out, ptr, entend - ptr);

	ctx->ptr = ptr;

	dump_content(ctx, ctx->idx);
}

void cmd_create(CTX)
{
	char* outfile = shift(ctx);
	char* start = shift(ctx);

	check_pac_ext(outfile);

	heap_init(ctx, 4*PAGE);

	ctx->dirbuf = heap_alloc(ctx, PAGE);
	ctx->dirlen = PAGE;

	scan_files(ctx, start);

	dump_packed(ctx, outfile);
}
