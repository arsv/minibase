#include <sys/proc.h>
#include <string.h>
#include <format.h>
#include <util.h>

static long execvpe_at(char** argv, char** envp, char* file, char* fend,
		char* dir, char* dend)
{
	int len = strelen(file, fend) + strelen(dir, dend) + 2;
	char* path = alloca(len);

	char* p = path;
	char* e = path + len - 1;

	p = fmtstre(p, e, dir, dend);
	p = fmtchar(p, e, '/');
	p = fmtstre(p, e, file, fend);

	*p++ = '\0';

	return sys_execve(path, argv, envp);
}

long execvpe(char* file, char** argv, char** envp)
{
	char* fend = strpend(file);

	if(!file)
		return -EFAULT;
	if(!fend)
		return -ENAMETOOLONG;
	if(fend == file)
		return -ENOENT;

	/* short-circuit to execve when something resembling a path is supplied */
	if(strecbrk(file, fend, '/') < fend)
		return sys_execve(file, argv, envp);

	char* p = getenv(envp, "PATH");
	char* e = strpend(p);

	/* it's a command and there's no $PATH defined */
	if(!p) return -ENOENT;
	if(!e) return -ENAMETOOLONG;

	while(p < e) {
		char* q = strecbrk(p, e, ':');

		int ret = execvpe_at(argv, envp, file, fend, p, q);

		/* we're still here, so execve failed */
		if((ret != -EACCES) && (ret != -ENOENT) && (ret != -ENOTDIR))
			return ret;

		p = q + 1;
	}

	return -ENOENT;
}
