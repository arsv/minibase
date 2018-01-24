#include <format.h>
#include <string.h>
#include <output.h>

#define BO struct bufout* bo

static void newline(BO)
{
	bufout(bo, "\n", 1);
}

static void dump(BO, int argc, char** argv, char** envp)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "argc=");
	p = fmtint(p, e, argc);
	p = fmtstr(p, e, " argv=0x");
	p = fmtxlong(p, e, (ulong)argv);
	p = fmtstr(p, e, " envp=0x");
	p = fmtxlong(p, e, (ulong)envp);
	FMTENL(p, e);

	bufout(bo, buf, p - buf);
}

static void dumparg(BO, int i, char* arg)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "[");
	p = fmtint(p, e, i);
	p = fmtstr(p, e, "] ");
	FMTEND(p, e);

	bufout(bo, buf, p - buf);
	bufout(bo, arg, strlen(arg));

	newline(bo);
}

int main(int argc, char** argv, char** envp)
{
	int i;
	char buf[1024];

	struct bufout bo = {
		.fd = STDOUT,
		.buf = buf,
		.ptr = 0,
		.len = sizeof(buf)
	};

	dump(&bo, argc, argv, envp);

	if(argc > 0)
		newline(&bo);
	for(i = 0; i < argc; i++)
		dumparg(&bo, i, argv[i]);

	bufoutflush(&bo);

	return 0;
}
