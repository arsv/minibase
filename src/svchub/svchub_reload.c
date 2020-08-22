#include <sys/file.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <printf.h>

#include "common.h"
#include "svchub.h"

/* The list of services to run in procs[] is a snapshot of INITDIR.
   INITDIR is assumed to be mostly static, but it does chance sometimes,
   and when it does procs[] must be updated to match.

   The code here rescans INITDIR, and updates procs[] accordingly. */

struct proc* find_by_name(CTX, char* name)
{
	int nprocs = ctx->nprocs;
	struct proc* rc = procs;
	struct proc* re = procs + nprocs;

	if(!name[0])
		return NULL;

	for(; rc < re; rc++)
		if(!strncmp(rc->name, name, sizeof(rc->name)))
			return rc;

	return NULL;
}

static struct proc* grab_proc_slot(CTX)
{
	int nprocs = ctx->nprocs;
	struct proc* rc = procs;
	struct proc* re = procs + nprocs;

	for(; rc < re; rc++)
		if(empty(rc))
			return rc;

	if(nprocs >= NPROCS)
		return NULL;

	ctx->nprocs = nprocs + 1;

	return rc;
}

static int tryfile(CTX, int at, char* base)
{
	int blen = strlen(base);
	struct proc* rc;
	struct stat st;

	if(sys_fstatat(at, base, &st, 0))
		goto out;
	if((st.mode & S_IFMT) != S_IFREG)
		goto out;
	if(!(st.mode & 0111)) /* not executable? */
		goto out;
	if(blen > NAMELEN - 1) /* name too long */
		goto out;

	if((rc = find_by_name(ctx, base))) {
		/* we have this one already */
		rc->flags &= ~P_STALE;
	} else if((rc = grab_proc_slot(ctx))) {
		memzero(rc, sizeof(*rc));
		memcpy(rc->name, base, blen);
		rc->fd = -1;
	} else {
		return -ENOMEM;
	}
out:
	return 0;
}

static int load_dir_ents(CTX)
{
	char* dir = INITDIR;
	int fd, ret;
	char buf[2048];

	if((fd = sys_open(dir, O_RDONLY | O_DIRECTORY)) < 0)
		return fd;

	while((ret = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* ptr = buf;
		char* end = buf + ret;

		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;

			if(de->type == DT_UNKNOWN)
				;
			else if(de->type == DT_REG)
				;
			else if(de->type == DT_LNK)
				;
			else continue;

			if((ret = tryfile(ctx, fd, de->name)) < 0)
				goto out;
		}
	}
out:
	sys_close(fd);

	return ret;
}

/* Once confdir has been re-scanned, two kinds of proc entries may need
   immediate attention:

       * new entries in confdir that have been just added to procs[], and
       * old entries procs[] which are no longer in confdir

   The former need to be started, the latter stopped and dropped. */

static void clear_stale_marks(CTX, int ret)
{
	struct proc* rc = procs;
	struct proc* re = procs + ctx->nprocs;

	for(; rc < re; rc++) {
		int flags = rc->flags;

		if(empty(rc))
			continue;

		if(ret < 0) { /* reload failed, drop new entries */
			if(flags & P_STALE) /* old proc, not reloaded */
				rc->flags = flags & ~P_STALE;
			else                /* new proc, drop it */
				free_proc_slot(ctx, rc);
		} else { /* reload successful, drop stale entries */
			if(flags & P_STALE) /* old proc gone from confdir */
				stop_proc(ctx, rc);
			else                /* new proc, start it */
				start_proc(ctx, rc);
		}
	}
}

static void mark_procs_stale(CTX)
{
	struct proc* rc = procs;
	struct proc* re = procs + ctx->nprocs;

	for(; rc < re; rc++) {
		if(!empty(rc))
			rc->flags |= P_STALE;
	}
}

int reload_procs(CTX)
{
	mark_procs_stale(ctx);

	int ret = load_dir_ents(ctx);

	clear_stale_marks(ctx, ret);

	return ret;
}
