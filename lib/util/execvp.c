#include <sys/proc.h>
#include <string.h>
#include <util.h>

static long execvpe_at(char* file, char** argv, char** envp,
		int flen, char* dir, int dlen)
{
	int len = flen + dlen + 1;
	char path[len+1];
	char* p = path;

	memcpy(p, dir, dlen); p += dlen;
	*p++ = '/';
	memcpy(p, file, flen); p += flen;
	*p++ = '\0';

	return sys_execve(path, argv, envp);
}

static int lookslikepath(const char* file)
{
	const char* p;

	for(p = file; *p; p++)
		if(*p == '/')
			return 1;

	return 0;
}

long execvpe(char* file, char** argv, char** envp)
{
	/* short-circuit to execve when something resembling path is supplied */
	if(lookslikepath(file))
		return sys_execve(file, argv, envp);

	char* p = getenv(envp, "PATH");
	char* e;
	int flen = strlen(file);

	/* it's a command and there's no $PATH defined */
	if(!p) return -ENOENT;

	while(*p) {
		for(e = p; *e && *e != ':'; e++);

		long ret = execvpe_at(file, argv, envp, flen, p, e - p);

		/* we're still here, so execve failed */
		if((ret != -EACCES) && (ret != -ENOENT) && (ret != -ENOTDIR))
			return ret;

		p = *e ? e + 1 : e;
	}

	return -ENOENT;
}
