#include <sys/mman.h>
#include <sys/file.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"

/* Heap routines. See msh.h for heap layout. */

#define PAGE 4096

void hinit(CTX)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + 4096);

	if(brk_error(brk, end))
		quit(ctx, "heap init failed", NULL, 0);

	ctx->heap = brk;
	ctx->esep = NULL;
	ctx->csep = brk;
	ctx->hptr = brk;
	ctx->hend = end;
}

void* halloc(CTX, int len)
{
	void* ret = ctx->hptr;

	if(ctx->hptr + len < ctx->hend)
		goto ptr;

	int spc = ctx->hend - ctx->hptr - len;
	spc += (PAGE - spc % PAGE) % PAGE;
	ctx->hend = sys_brk(ctx->hend + spc);

	if(mmap_error(ctx->hend))
		quit(ctx, "cannot allocate memory", NULL, 0);
ptr:
	ctx->hptr += len;

	return ret;
}

void hrev(CTX, int what)
{
	switch(what) {
		case CSEP:
			ctx->hptr = ctx->csep;
			break;
		case ESEP:
			ctx->csep = NULL;
			ctx->hptr = ctx->esep;
			break;
		case HEAP:
			ctx->esep = NULL;
			ctx->csep = NULL;
			ctx->hptr = ctx->heap;
	}
}

void hset(CTX, int what)
{
	switch(what) {
		case CSEP: ctx->csep = ctx->hptr; break;
		case ESEP: ctx->esep = ctx->hptr; break;
	}
}

int mmapfile(struct mbuf* mb, char* name)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY | O_CLOEXEC)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		goto out;
	/* get larger-than-int files out of the picture */
	if(st.size > 0x7FFFFFFF) {
		ret = -E2BIG;
		goto out;
	}

	void* ptr = sys_mmap(NULL, st.size, PROT_READ, MAP_SHARED, fd, 0);

	if(mmap_error(ptr)) {
		ret = (long)ptr;
		goto out;
	}

	mb->len = st.size;
	mb->buf = ptr;
	ret = 0;
out:
	sys_close(fd);
	return ret;
}

int munmapfile(struct mbuf* mb)
{
	return sys_munmap(mb->buf, mb->len);
}

static int mapid(struct mbuf* mb, char* name)
{
	char* filedata = mb->buf;
	char* fileend = filedata + mb->len;
	int id;

	/* user:x:500:...\n */
	/* ls  ue un     le */
	char *ls, *le;
	char *ue, *un;
	char *ne = NULL;
	for(ls = filedata; ls < fileend; ls = le + 1) {
		le = strecbrk(ls, fileend, '\n');
		ue = strecbrk(ls, le, ':');
		if(ue >= le) continue;
		un = strecbrk(ue + 1, le, ':') + 1;
		if(un >= le) continue;

		if(strncmp(name, ls, ue - ls))
			continue;

		ne = parseint(un, &id);
		break;
	};

	if(!ne || *ne != ':')
		return -1;

	return id;
}

static int prep_pwfile(CTX, struct mbuf* mb, char* pwfile)
{
	int ret;

	if(mb->buf)
		return 0;
	if((ret = mmapfile(mb, pwfile)) < 0)
		error(ctx, "cannot mmap", pwfile, ret);

	return 0;
}

static int resolve_pwname(CTX, char* name, int* id,
		struct mbuf* mb, char* file, char* notfound)
{
	int ret;
	char* p;

	if((p = parseint(name, id)) && !*p)
		return 0;

	prep_pwfile(ctx, mb, file);

	if((ret = mapid(mb, name)) < 0)
		fatal(ctx, notfound, name);

	*id = ret;

	return 0;
}

int get_user_id(CTX, char* user, int* uid)
{
	return resolve_pwname(ctx, user, uid,
			&ctx->passwd, "/etc/passwd", "unknown user name");
}

int get_group_id(CTX, char* group, int* gid)
{
	return resolve_pwname(ctx, group, gid,
			&ctx->passwd, "/etc/group", "unknown group name");
}

int get_owner_ids(CTX, char* owner, int* uid, int* gid)
{
	char* sep = strcbrk(owner, ':');

	if(!*sep || !*(sep+1)) /* "user" or "user:" */
		*gid = -1;
	else if(get_group_id(ctx, sep+1, gid))
		return -1;

	*sep = '\0';

	if(sep == owner) /* ":group" */
		*uid = -1;
	else if(get_user_id(ctx, owner, uid))
		return -1;

	return 0;
}
