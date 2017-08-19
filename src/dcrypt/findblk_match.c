#include <sys/file.h>

#include <format.h>
#include <string.h>
#include "findblk.h"

static int open_dev_dir(char* name)
{
	char* pref = "/sys/block/";
	char* post = "/device";
	int nlen = strlen(name);

	FMTBUF(p, e, path, 30 + nlen);

	if(pref) p = fmtstr(p, e, pref);
	p = fmtstr(p, e, name);
	if(post) p = fmtstr(p, e, post);

	FMTEND(p);

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

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* trim(char* p)
{
	char* e = p + strlen(p);

	while(p < e && isspace(*p)) p++;
	while(p < e && isspace(*(e-1))) e--;

	*e = '\0';

	return p;
}

static int compare_entry(int at, char* id, const char* ent, int off)
{
	char buf[100];
	int rd;
	
	if((rd = read_entry(at, ent, buf, sizeof(buf))) < 0)
		return 0;
	if(rd < off)
		return 0;

	char* p = trim(buf + off);

	return !strcmp(p, id);

}

static int by_name(char* id, char* name)
{
	return !strcmp(id, name);
}

static int by_pg80(int fd, char* id)
{
	return compare_entry(fd, id, "vpd_pg80", 4);
}

static int by_cid(int fd, char* id)
{
	return compare_entry(fd, id, "cid", 0);
}

static int match(int fd, struct bdev* bd, char* name)
{
	switch(bd->type) {
		case BY_NAME: return by_name(bd->id, name);
		case BY_PG80: return by_pg80(fd, bd->id);
		case BY_CID:  return by_cid(fd, bd->id);
		default: return 0;
	}
}

static void set_dev_name(struct bdev* bd, char* name)
{
	int nlen = strlen(name);

	if(nlen > sizeof(bd->name) - 1)
		quit("device name too long:", name, 0);

	bd->here = 1;
	memcpy(bd->name, name, nlen);
	bd->name[nlen] = '\0';
}

int match_dev(char* name)
{
	struct bdev* bd;
	int fd;

	if((fd = open_dev_dir(name)) < 0)
		return 0;

	for(bd = bdevs; bd < bdevs + nbdevs; bd++) {
		if(bd->here)
			continue;
		if(!match(fd, bd, name))
			continue;

		set_dev_name(bd, name);
		break;
	}

	sys_close(fd);

	return (bd < bdevs + nbdevs) ? 1 : 0;
}

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

static int is_named_part_of(char* dev, char* part, char* name)
{
	int dl = strlen(dev);
	int nl = strlen(name);

	if(dl > nl)
		return 0;
	if(strncmp(name, dev, dl))
		return 0;

	if(isdigit(dev[dl-1]) && name[dl] == 'p')
		dl++;
	if(strcmp(name + dl, part))
		return 0;

	return 1;
}

static void set_part_name(struct part* pt, char* name)
{
	int nlen = strlen(name);

	if(nlen > sizeof(pt->name) - 1)
		quit("part name too long:", name, 0);

	if(pt->here)
		return;

	pt->here = 1;
	memcpy(pt->name, name, nlen);
	pt->name[nlen] = '\0';
}

void match_part(char* name)
{
	struct part* pt;
	struct bdev* bd;

	for(pt = parts; pt < parts + nparts; pt++) {
		bd = &bdevs[pt->devidx];

		if(pt->here || !bd->here)
			continue;
		if(!is_named_part_of(bd->name, pt->part, name))
			continue;

		set_part_name(pt, name);
		break;
	}
}
