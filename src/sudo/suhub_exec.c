#include <sys/proc.h>
#include <sys/file.h>
#include <sys/creds.h>
#include <sys/fprop.h>
#include <sys/fpath.h>

#include <cmsg.h>
#include <format.h>
#include <string.h>
#include <fail.h>
#include <exit.h>

#include "config.h"
#include "suhub.h"

/* This is the only piece here that runs in the context of the child process.
   The passed fds[] are CLOEXEC, no need to close them explicitly.
   In fact, everything except 0, 1, 2 should be CLOEXEC here. */

static int child(char* path, char** argv, int fds[4], int uid, int gid)
{
	xchk(sys_setresgid(gid, -1, gid), "setgid", NULL);
	xchk(sys_setresuid(gid, -1, uid), "setuid", NULL);

	xchk(sys_dup2(fds[0], 0), "dup2", "stdin");
	xchk(sys_dup2(fds[1], 1), "dup2", "stdout");
	xchk(sys_dup2(fds[2], 2), "dup2", "stderr");
	xchk(sys_fchdir(fds[3]), "fchdir", NULL);

	int ret = sys_execve(path, argv, environ);
	fail("exec", path, ret);
}

/* This is the critical permission-checking step -- sudo should only
   ever allow exec'ing basenames in CONFDIR. */

static int is_valid_command(char* cmd)
{
	char* p;

	if(!cmd || !*cmd || *cmd == '.')
		return 0;

	for(p = cmd; *p; p++)
		if(*p == '/')
			return 0;

	return 1;
}

/* We do a quick access() check here to avoid forking if the file
   doesn't even exist. A false positive is ok, subsequent exec will
   report that anyway. The caller makes sure the arguments are ok. */

int spawn(int* cpid, char** argv, int* fds, struct ucred* cr)
{
	char* pref = CONFDIR;
	int ret;

	if(!is_valid_command(*argv))
		return -EACCES;

	FMTBUF(p, e, path, strlen(pref) + strlen(*argv) + 4);
	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, *argv);
	FMTEND(p);

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;

	if((ret = sys_fork()) < 0)
		return ret;
	if(ret == 0)
		_exit(child(path, argv, fds, cr->uid, cr->gid));

	*cpid = ret;

	return 0;
}
