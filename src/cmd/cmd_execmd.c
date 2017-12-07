#include <sys/proc.h>
#include <util.h>

#include "cmd.h"

void execute(CTX, int argc, char** argv)
{
	char** envp = ctx->envp;
	int ret, pid, status;

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	}

	if(pid == 0) {
		ret = execvpe(*argv, argv, envp);
		fail(NULL, *argv, ret);
	}

	if((ret = sys_waitpid(pid, &status, 0)) < 0) {
		warn("wait", NULL, ret);
		return;
	}

	if(WIFSIGNALED(status))
		warn("killed by signal", NULL, WTERMSIG(status));
}
