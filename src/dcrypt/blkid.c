#include <sys/file.h>
#include <sys/stat.h>

#include <format.h>
#include <string.h>
#include <fail.h>

ERRTAG = "blkid";
ERRLIST = {
	REPORT(ENOENT), REPORT(EINVAL), RESTASNUMBERS
};

static int open_dev_dir(char* name)
{
	char* pref = "/sys/block/";
	char* post = "/device";
	int nlen = strlen(name);
	char path[30+nlen];

	char* p = path;
	char* e = path + sizeof(path) - 1;

	if(pref)
		p = fmtstr(p, e, pref);

	p = fmtstr(p, e, name);

	if(post)
		p = fmtstr(p, e, post);

	*p++ = '\0';

	return sys_open(path, O_DIRECTORY);
}

static int read_entry(int at, const char* entry, char* buf, int len)
{
	int fd, rd;

	if((fd = sys_openat(at, entry, O_RDONLY)) < 0)
		return fd;

	rd = sys_read(fd, buf, len-1);

	sys_close(fd);

	if(rd > 0 && buf[rd-1] == '\n')
		rd--;

	buf[rd] = '\0';

	return rd;
}

static int printable(void* value, int len)
{
	unsigned char* p = value;
	unsigned char* e = value + len;

	for(; p < e; p++)
		if(*p < 0x20 || *p >= 0x7F)
			return 0;

	return 1;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\t');
}

static int has_any_special(char* p, char* e)
{
	for(; p < e; p++)
		if(isspace(*p))
			break;
		else if(*p == '\\' || *p == '"')
			break;

	return (p < e);
}

static char* fmt_id(char* p, char* e, char* val)
{
	char* vp = val;
	char* ve = val + strlen(val);

	while(vp < ve && isspace(*vp))
		vp++;
	while(ve > vp && isspace(*(ve - 1)))
		ve--;

	int quotes = has_any_special(vp, ve);

	if(quotes)
		p = fmtchar(p, e, '"');

	for(; vp < ve; vp++) {
		if(*vp == '"' || *vp == '\\')
			p = fmtchar(p, e, '\\');
		p = fmtchar(p, e, *vp);
	}

	if(quotes)
		p = fmtchar(p, e, '"');

	return p;
}

static int out_dev(const char* tag, char* value)
{
	char buf[200];
	char* p = buf;
	char* e = buf + sizeof(buf) - 2;

	p = fmtstr(p, e, tag);
	p = fmtstr(p, e, " ");
	p = fmt_id(p, e, value);

	*p++ = '\n';
	*p = '\0';

	sys_write(STDOUT, buf, p - buf);

	return 1;
}

static int printable_entry_id(int fd, const char* name, int off, const char* tag)
{
	char buf[100];
	int len;
	
	if((len = read_entry(fd, name, buf, sizeof(buf))) <= 0)
		return 0;
	if(len < off)
		return 0;
	if(!printable(buf + off, len - off))
		return 0;

	return out_dev(tag, buf + off);
}

static int use_pg80(int fd)
{
	return printable_entry_id(fd, "vpd_pg80", 4, "serial");
}

static int use_cid(int fd)
{
	return printable_entry_id(fd, "cid", 0, "cid");
}

static void identify(char* name)
{
	int fd;

	if((fd = open_dev_dir(name)) < 0)
		fail("no /sys entry for", name, 0);

	if(use_pg80(fd))
		return;
	if(use_cid(fd))
		return;

	fail("no suitable identification token for", name, 0);
}

int main(int argc, char** argv)
{
	int i = 1;

	if(i >= argc)
		fail("too few arguments", NULL, 0);
	for(; i < argc; i++)
		identify(argv[i]);

	return 0;
}
