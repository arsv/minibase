#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/dents.h>
#include <sys/prctl.h>
#include <sys/proc.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <string.h>
#include <format.h>
#include <printf.h>
#include <config.h>
#include <main.h>
#include <util.h>

#include "mpac.h"

void heap_init(CTX, int size)
{
	int need = pagealign(size);

	void* brk = sys_brk(NULL);
	void* end = sys_brk(brk + need);

	if(brk_error(brk, end))
		quit(ctx, "brk", NULL, -ENOMEM);

	ctx->brk = brk;
	ctx->ptr = brk;
	ctx->end = end;
}

void* heap_alloc(CTX, int size)
{
	void* ptr = ctx->ptr;
	void* end = ctx->end;
	void* new = ptr + size;

	if(new > end) {
		void* tmp = sys_brk(ptr + pagealign(size));

		if(brk_error(end, tmp))
			quit(ctx, "brk", NULL, -ENOMEM);

		ctx->end = end = tmp;
	}

	ctx->ptr = new;

	return ptr;
}

void heap_reset(CTX, void* ptr)
{
	ctx->ptr = ptr;
}

static void check_extension(char* name, char* suff)
{
	int nlen = strlen(name);
	int slen = strlen(suff);

	if(nlen <= slen)
		;
	else if(!strncmp(name + nlen - slen, suff, 4))
		return;

	FMTBUF(p, e, msg, 100);
	p = fmtstr(p, e, "not a ");
	p = fmtstr(p, e, suff);
	p = fmtstr(p, e, " file:");
	FMTEND(p, e);

	fail(msg, name, 0);
}

void check_pac_ext(char* name)
{
	check_extension(name, ".pac");
}

void check_list_ext(char* name)
{
	check_extension(name, ".list");
}

static void parse_header_size(CTX, byte tag[8])
{
	if(memcmp(tag, "PAC", 3))
		fail("not a PAC file", NULL, 0);

	int n = tag[3];

	if((n & ~3))
		fail("not a PAC file", NULL, 0);

	uint size;
	byte* sz = tag + 4;

	size = sz[0];

	if(n > 0)
		size |= (sz[1] << 8);
	if(n > 1)
		size |= (sz[2] << 16);
	if(n > 2) /* header size over 16MB, yikes */
		fail("4-byte index size", NULL, 0);

	uint start = 4 + n + 1;

	ctx->hoff = start;
	ctx->hlen = size;
}

static void parse_size(CTX, byte lead)
{
	byte* ptr = ctx->iptr;
	byte* end = ctx->iend;
	int n = lead & TAG_SIZE;

	uint size = 0;

	if(ptr + n + 1> end)
		quit(ctx, "truncated index", NULL, 0);

	size = *ptr++;

	if(n > 0)
		size |= (*ptr++) << 8;
	if(n > 1)
		size |= (*ptr++) << 16;
	if(n > 2)
		size |= (*ptr++) << 24;

	ctx->size = size;
	ctx->iptr = ptr;
}

static void parse_name(CTX)
{
	byte* ptr = ctx->iptr;
	byte* end = ctx->iend;
	byte* tmp = ptr;

	while(ptr < end && *ptr)
		ptr++;
	if(ptr >= end)
		quit(ctx, "non-terminated name in index", NULL, 0);

	char* name = (char*)tmp;

	if(dotddot(name) || !!strchr(name, '/'))
		quit(ctx, "invalid entry name", name, 0);

	ctx->name = name;
	ctx->nlen = ptr - tmp;
	ctx->iptr = ptr + 1;
}

void load_index(CTX)
{
	byte tag[8];
	int ret, fd = ctx->fd;

	if((ret = sys_read(fd, tag, sizeof(tag))) < 0)
		fail("read", NULL, ret);
	if(ret < (int)sizeof(tag))
		fail("package index too short", NULL, 0);

	parse_header_size(ctx, tag);

	uint hoff = ctx->hoff;
	uint hlen = ctx->hlen;

	if(hoff > 8 || hoff + hlen < 8)
		fail("malformed package", NULL, 0);

	uint got = sizeof(tag) - ctx->hoff;
	uint need = hlen - got;
	uint full = hoff + hlen;

	byte* head = heap_alloc(ctx, (full + 3) & ~3);
	byte* rest = head + sizeof(tag);

	memcpy(head, tag, sizeof(tag));

	if((ret = sys_read(fd, rest, need)) < 0)
		fail("read", NULL, ret);
	if(ret < (int)need)
		fail("incomplete read", NULL, 0);

	ctx->head = head;
	ctx->iptr = head + hoff;
	ctx->iend = head + hoff + hlen;
}

static void alloc_transfer_buf(CTX)
{
	uint size = 1<<20;
	uint prot = PROT_READ | PROT_WRITE;
	uint flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);
	int ret;

	if(ctx->databuf && ctx->datasize != size)
		fail(NULL, NULL, -EFAULT);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	ctx->databuf = buf;
	ctx->datasize = size;
}

static void spawn_pipe(CTX, char* dec, char* path)
{
	int ret, pid, fds[2];

	alloc_transfer_buf(ctx);

	if((ret = sys_pipe(fds)) < 0)
		fail("pipe", NULL, ret);

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, 0);

	if(pid == 0) {
		char* args[] = { dec, path, NULL };

		if((ret = sys_prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) < 0)
			fail("prctl", NULL, ret);
		if((ret = sys_dup2(fds[1], 1)) < 0)
			fail("dup2", NULL, ret);
		if((ret = sys_close(fds[0])) < 0)
			fail("close", NULL, ret);
		if((ret = sys_close(fds[1])) < 0)
			fail("close", NULL, ret);

		ret = sys_execve(*args, args, ctx->envp);

		fail("execve", *args, ret);
	}

	if((ret = sys_close(fds[1])) < 0)
		fail("close", NULL, ret);

	ctx->fd = fds[0];
}

static void open_compressed(CTX, char* name, char* suff)
{
	int ret;

	if(*suff != '.')
		fail("invalid compression suffix", suff, 0);

	char* rest = suff + 1; /* skip the dot */

	if(!*rest)
		fail("invalid compression suffix", NULL, 0);

	char* pref = BASE_ETC "/mpac/";
	int plen = strlen(pref);
	int slen = strlen(suff);

	int len = plen + slen + 2;
	char* buf = alloca(len);
	char* p = buf;
	char* e = p + len - 1;

	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, rest);

	*p++ = '\0';

	if((ret = sys_access(buf, X_OK)) < 0)
		fail(NULL, buf, ret);

	spawn_pipe(ctx, buf, name);
}

static void open_uncompressed(CTX, char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	ctx->fd = fd;
}

static char* skip_extension(char* p, char* e)
{
	while(p < e)
		if(*(--e) == '.')
			break;

	return e;
}

static int equals(char* p, char* e, char* str)
{
	int slen = strlen(str);
	int plen = e - p;

	if(slen != plen)
		return 0;

	return !memcmp(p, str, plen);
}

void open_pacfile(CTX, char* name)
{
	char* nend = strpend(name);
	char* suff = skip_extension(name, nend);

	if(equals(suff, nend, ".pac"))
		return open_uncompressed(ctx, name);

	char* prev = skip_extension(name, suff);

	if(equals(prev, suff, ".pac"))
		return open_compressed(ctx, name, suff);

	fail("no .pac suffix in", name, 0);
}

int next_entry(CTX)
{
	byte* ptr = ctx->iptr;
	byte* end = ctx->iend;

	if(ptr >= end)
		return -1;

	byte lead = *ptr;
	ctx->iptr = ptr + 1;

	ctx->name = NULL;
	ctx->nlen = 0;
	ctx->size = 0;

	if(lead & TAG_DIR) {
		parse_name(ctx);
	} else {
		parse_size(ctx, lead);
		parse_name(ctx);
	}

	return lead;
}

void failx(CTX, const char* msg, char* name, int ret)
{
	char* root = ctx->root;
	int i, n = ctx->depth;
	int len = 2;

	len += strlen(root) + 1;

	for(i = 0; i < n; i++)
		len += strlen(ctx->path[i]) + 1;

	len += strlen(name);

	char* buf = alloca(len);

	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, ctx->root);
	p = fmtstr(p, e, "/");

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, ctx->path[i]);
		p = fmtstr(p, e, "/");
	}

	p = fmtstr(p, e, name);

	*p++ = '\0';

	fail(msg, buf, ret);
}
