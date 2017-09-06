#include <sys/file.h>

#include <errtag.h>
#include <format.h>
#include <util.h>

ERRTAG("holes");

static uint64_t lseek(int fd, uint64_t off, int whence)
{
	uint64_t ret;

	if((ret = sys_lseek(fd, off, whence)) < 0)
		fail("seek", NULL, ret);

	return ret;
}

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
	uint64_t ds, dh;

	if(argc != 2)
		fail("bad call", NULL, 0);

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	ds = lseek(fd, 0, SEEK_DATA);

	if(ds > 0) report("hole", 0, ds);

	while(ds < st.size) {
		dh = lseek(fd, ds, SEEK_HOLE);

		report("data", ds, dh);

		if(dh >= st.size)
			break;

		ds = lseek(fd, dh, SEEK_DATA);

		report("hole", dh, ds);
	}

	return 0;
}
