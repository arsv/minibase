#include <sys/file.h>
#include <sys/fprop.h>

#include <string.h>
#include <format.h>

#define TAG "which"

extern void _exit(int) __attribute__((noreturn));

static void warn(const char* obj, const char* msg)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, TAG ": ");

	if(!obj) goto no;

	p = fmtstr(p, e, obj);
	p = fmtstr(p, e, " ");
no:
	p = fmtstr(p, e, msg);
	p = fmtstr(p, e, "\n");

	sys_write(2, buf, p - buf);
}

static char* xgetenv(char** envp, char* var)
{
	char** p;
	int len = strlen(var);

	for(p = envp; *p; p++)
		if(!strncmp(*p, var, len))
			return *p + len;

	warn(NULL, "$PATH is not set");
	_exit(-1);
}

static int execheck(char* dir, int dirlen, char* cmd, int cmdlen)
{
	char path[dirlen + cmdlen + 4];
	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtraw(p, e, dir, dirlen);
	p = fmtstr(p, e, "/");
	p = fmtraw(p, e, cmd, cmdlen);
	*p = '\0';

	if(sys_access(path, X_OK) < 0)
		return 0;
	
	*p++ = '\n';
	sys_write(1, path, p - path);

	return -1;
}

static int which(char* path, char* cmd, int cmdlen)
{ 
	char* pend = path + strlen(path);
	char* p;
	char* q;

	for(p = path; p < pend; p = q + 1) {
		for(q = p; *q && *q != ':'; q++)
			;
		if(execheck(p, q - p, cmd, cmdlen))
			return 0;
	}

	warn(cmd, "not found in $PATH");
	return -1;
}

int main(int argc, char** argv, char** envp)
{
	int i;
	char* path = xgetenv(envp, "PATH=");
	int ret = 0;

	for(i = 1; i < argc; i++)
		ret |= which(path, argv[i], strlen(argv[i]));

	return ret;
}
