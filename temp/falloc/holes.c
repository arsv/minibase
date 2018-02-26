#include <sys/file.h>

#include <errtag.h>
#include <format.h>
#include <util.h>

ERRTAG("holes");

static void report(char* tag, uint64_t from, uint64_t to)
{
	FMTBUF(p, e, buf, 100);
	p = fmtpad0(p, e, 8, fmtlong(p, e, from));
	p = fmtstr(p, e, " ");
	p = fmtpad0(p, e, 8, fmtlong(p, e, to));
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, " len ");
	p = fmtlong(p, e, to - from);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

int main(int argc, char** argv)
{
	int fd, ret;
	char* name = argv[1];
	struct stat st;
	off_t ds = 0, dh = 0;

	if(argc != 2)
		fail("bad call", NULL, 0);

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	if((ret = sys_llseek(fd, 0, &ds, SEEK_DATA)) < 0)
		fail("llseek", "DATA", ret);
	if(ds > 0)
		report("hole", 0, ds);

	while(ds < st.size) {
		if((ret = sys_llseek(fd, ds, &dh, SEEK_HOLE)) < 0)
			fail("llseek", "HOLE", ret);

		report("data", ds, dh);

		if(dh >= st.size)
			break;

		if((ret = sys_llseek(fd, dh, &ds, SEEK_DATA)) < 0)
			fail("llseek", "DATA", ret);

		report("hole", dh, ds);
	}

	return 0;
}
