#include <sys/file.h>
#include <sys/dents.h>

#include <format.h>
#include <main.h>
#include <util.h>

ERRTAG("lsdents");

static char entry_type(struct dirent* de)
{
	switch(de->type) {
		case DT_BLK: return 'b';
		case DT_CHR: return 'c';
		case DT_DIR: return '/';
		case DT_FIFO: return '|';
		case DT_LNK: return '@';
		case DT_REG: return ' ';
		case DT_SOCK: return '=';
		case DT_UNKNOWN: return '?';
		default: return 'x';
	}
}

static void dump_entry(struct dirent* de)
{
	char type = entry_type(de);

	FMTBUF(p, e, buf, 512);
	p = fmtchar(p, e, type);
	p = fmtchar(p, e, ' ');
	p = fmtstr(p, e, de->name);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

int main(int argc, char** argv)
{
	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 2)
		fail("too many arguments", NULL, 0);

	int fd, rd;
	char* dir = argv[1];
	char buf[2048];

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* p = buf;
		void* e = buf + rd;

		while(p < e) {
			struct dirent* de = p;

			if(!de->reclen)
				continue;

			p += de->reclen;

			if(dotddot(de->name))
				continue;

			dump_entry(de);
		}
	} if(rd < 0) {
		fail("getdents", dir, rd);
	}

	return 0;
}
