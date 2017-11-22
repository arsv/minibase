#include <sys/file.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "findblk.h"

/* Partitions are only matched by their device name: when "sda1" appears
   in the system, we check if there's a partition 1 entry on an pre-matched
   device named "sda". No /sys check of any kind are performed. Filesystem
   type is checked later, possibly after decrypting the devices, and that's
   a fatal check, not a matching one. */

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
	uint nlen = strlen(name);

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

		return;
	}
}

/* The rest is device matching code */

static int open_dev(char* name)
{
	char* pref = "/dev/";

	FMTBUF(p, e, path, strlen(pref) + strlen(name) + 4);
	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	return sys_open(path, O_RDONLY);
}

static int read_entry(char* dev, const char* entry, char* buf, int len)
{
	int fd, rd;

	FMTBUF(p, e, path, strlen(dev) + strlen(entry) + 50);
	p = fmtstr(p, e, "/sys/block/");
	p = fmtstr(p, e, dev);
	p = fmtstr(p, e, "/device/");
	p = fmtstr(p, e, entry);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
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

static int compare_sys_entry(char* dev, char* id, const char* ent, int off)
{
	char buf[100];
	int rd;
	
	if((rd = read_entry(dev, ent, buf, sizeof(buf))) < 0)
		return 0;
	if(rd < off)
		return 0;

	char* p = trim(buf + off);

	return !strcmp(p, id);

}

static int is_named(char* dev, char* id)
{
	return !strcmp(dev, id);
}

static int has_pg80(char* dev, char* id)
{
	return compare_sys_entry(dev, id, "vpd_pg80", 4);
}

static int has_cid(char* dev, char* id)
{
	return compare_sys_entry(dev, id, "cid", 0);
}

static int cmple4(void* got, char* req)
{
	uint8_t* bp = got;

	FMTBUF(p, e, buf, 10);
	p = fmtbyte(p, e, bp[3]);
	p = fmtbyte(p, e, bp[2]);
	p = fmtbyte(p, e, bp[1]);
	p = fmtbyte(p, e, bp[0]);
	FMTEND(p, e);

	return strcmp(buf, req);
}

static const char mbrtag[] = { 0x55, 0xAA };

static int has_mbr(char* dev, char* id)
{
	int fd, rd, ret = 0;
	char buf[0x200];

	if((fd = open_dev(dev)) < 0)
		return 0;
	if((rd = sys_read(fd, buf, sizeof(buf))) < 0)
		goto out;
	if((ulong)rd < sizeof(buf))
		goto out;

	if(memcmp(buf + 0x1FE, mbrtag, sizeof(mbrtag)))
		goto out;
	if(cmple4(buf + 0x1B8, id))
		goto out;

	ret = 1;
out:
	sys_close(fd);
	return ret;
}

static const char efitag[] = "EFI PART";

static int load_gpt_at(int fd, int off, void* buf, int size)
{
	int ret;

	if((ret = sys_seek(fd, off)) < 0)
		return 0;
	if((ret = sys_read(fd, buf, size)) < 0)
		return 0;
	if(ret < size)
		return 0;

	if(memcmp(buf, efitag, sizeof(efitag) - 1))
		return 0;

	return 1;
}

static char* fmtlei32(char* p, char* e, void* buf)
{
	uint8_t* v = buf;

	p = fmtbyte(p, e, v[3]);
	p = fmtbyte(p, e, v[2]);
	p = fmtbyte(p, e, v[1]);
	p = fmtbyte(p, e, v[0]);

	return p;
}

static char* fmtlei16(char* p, char* e, void* buf)
{
	uint8_t* v = buf;

	p = fmtbyte(p, e, v[1]);
	p = fmtbyte(p, e, v[0]);

	return p;
}

static int check_gpt_guid(void* buf, char* id)
{
	/* The first 4 bytes of GUID are stored as little-endian int32,
	   the following 4 as two little-endian int16-s, and the rest is
	   stored as is. Why? Because fuck you that's why! -- somebody
	   at the GPT committee, probably.  */

	FMTBUF(p, e, guid, 32 + 2);
	p = fmtlei32(p, e, buf + 0x38 + 0);
	p = fmtlei16(p, e, buf + 0x38 + 4);
	p = fmtlei16(p, e, buf + 0x38 + 6);
	p = fmtbytes(p, e, buf + 0x38 + 8, 8);
	FMTEND(p, e);

	return strncmp(guid, id, 32);
}

static int has_gpt(char* dev, char* id)
{
	int fd, ret = 0;
	char buf[0x64];

	if((fd = open_dev(dev)) < 0)
		return 0;

	if(load_gpt_at(fd, 512, buf, sizeof(buf)))
		goto got;
	if(load_gpt_at(fd, 4096, buf, sizeof(buf)))
		goto got;
	goto out;

got:
	if(check_gpt_guid(buf, id))
		goto out;
	ret = 1;

out:
	sys_close(fd);
	return ret;
}

static int matches(struct bdev* bd, char* dev)
{
	switch(bd->how) {
		case BY_NAME: return is_named(dev, bd->id);
		case BY_PG80: return has_pg80(dev, bd->id);
		case BY_CID:  return has_cid(dev, bd->id);
		case BY_MBR:  return has_mbr(dev, bd->id);
		case BY_GPT:  return has_gpt(dev, bd->id);
		default: return 0;
	}
}

static void set_dev_name(struct bdev* bd, char* name)
{
	uint nlen = strlen(name);

	if(nlen > sizeof(bd->name) - 1)
		quit("device name too long:", name, 0);

	bd->here = 1;
	memcpy(bd->name, name, nlen);
	bd->name[nlen] = '\0';
}

static void set_whole(int devidx, char* name)
{
	struct part* pt;

	for(pt = parts; pt < parts + nparts; pt++)
		if(pt->devidx == devidx && !pt->name[0])
			set_part_name(pt, name);
}

int match_dev(char* name)
{
	struct bdev* bd;

	for(bd = bdevs; bd < bdevs + nbdevs; bd++) {
		if(bd->here)
			continue;
		if(!matches(bd, name))
			continue;

		set_dev_name(bd, name);

		if(bd->mode == WHOLE)
			set_whole(bd - bdevs, name);

		return 1;
	}

	return 0;
}
