#include <sys/file.h>
#include <sys/mman.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "common.h"
#include "ifmon.h"

#define PAGE 4096
#define MAX_CONFIG_SIZE 64*1024

struct line {
	char* start;
	char* end;
};

struct chunk {
	char* start;
	char* end;
};

static char* config;
static int blocklen;
static int datalen;
static int modified;

static int pagealign(long size)
{
	return size + (PAGE - size % PAGE) % PAGE;
}

void save_config(void)
{
	int fd;
	char* name = IFCFG;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;

	if(!config)
		return;
	if(!modified)
		return;

	if((fd = sys_open3(name, flags, 0600)) < 0) {
		warn("cannot open", name, fd);
		return;
	}

	writeall(fd, config, datalen);
	sys_close(fd);
	modified = 0;
}

void drop_config(void)
{
	if(!config)
		return;
	if(modified)
		save_config();

	sys_munmap(config, blocklen);
	config = NULL;
	blocklen = 0;
	datalen = 0;
}

static int open_stat_config(char* name, int* size)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0) {
		*size = 0;
		return fd;
	}

	if((ret = sys_fstat(fd, &st)) < 0)
		goto out;
	if(st.size > MAX_CONFIG_SIZE) {
		ret = -E2BIG;
		goto out;
	}

	*size = st.size;
	return fd;
out:
	sys_close(fd);

	if(ret && ret != -ENOENT)
		warn(NULL, name, ret);

	return ret;
}

static int mmap_config_buf(int filesize)
{
	int size = pagealign(filesize + 1024);
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void* ptr = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(ptr))
		return (long)ptr;

	config = ptr;
	blocklen = size;
	datalen = 0;
	modified = 0;

	return 0;
}

static int read_config_whole(int fd, int filesize)
{
	int ret;

	if(filesize <= 0)
		return 0;
	if((ret = sys_read(fd, config, filesize)) < filesize)
		return ret;

	return 0;
}

static int remap_config(int len)
{
	int lenaligned = len + (PAGE - len % PAGE) % PAGE;
	int newblocklen = blocklen + lenaligned;

	void* ptr = sys_mremap(config, blocklen, newblocklen, MREMAP_MAYMOVE);

	if(mmap_error(ptr))
		return (long)ptr;

	config = ptr;
	blocklen = len;

	return 0;
}

static int extend_config(int len)
{
	int ret, newdl = datalen + len;

	if(newdl < 0)
		return -EINVAL;

	if(newdl > blocklen)
		if((ret = remap_config(newdl)) < 0)
			return ret;

	datalen = newdl;

	return 0;
}

int load_config(void)
{
	int fd;
	long ret;
	int filesize;
	char* name = IFCFG;

	if(config)
		return 0;

	if((fd = open_stat_config(name, &filesize)) < 0)
		filesize = 0;
	if((ret = mmap_config_buf(filesize)) < 0)
		goto out;
	if((ret = read_config_whole(fd, filesize)) < 0)
		goto out;

	datalen = filesize;
	ret = 0;
out:	
	if(fd >= 0)
		sys_close(fd);
	if(ret && ret != -ENOENT)
		warn(NULL, name, ret);

	return ret;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n');
}

static char* skipline(char* p, char* e)
{
	while(p < e && *p != '\n')
		p++;
	return p;
}

static int setline(struct line* ln, char* p)
{
	char* confend = config + datalen;

	if(p >= confend)
		p = NULL;
	else if(p < config)
		p = NULL;

	ln->start = p ? p : NULL;
	ln->end = p ? skipline(p, confend) : NULL;

	return !!p;
}

static int firstline(struct line* ln)
{
	return setline(ln, config);
}

static int nextline(struct line* ln)
{
	return setline(ln, ln->end + 1);
}

static char* skiparg(char* p, char* e)
{
	for(; p < e && !isspace(*p); p++)
		if(*p == '\\') p++;
	return p;
}

static char* skipsep(char* p, char* e)
{
	while(p < e && isspace(*p))
		p++;
	return p;
}

int split_line(struct line* ln, struct chunk* ck, int nc)
{
	char* end = ln->end;
	char* p = ln->start;
	int i = 0;

	p = skipsep(p, end);

	while(p < end && i < nc) {
		struct chunk* ci = &ck[i++];
		ci->start = p;
		p = skiparg(p, end);
		ci->end = p;
		p = skipsep(p, end);
	}

	return i;
}

int chunklen(struct chunk* ck)
{
	return ck->end - ck->start;
}

static int chunkeq(struct chunk* ck, const char* str, int len)
{
	if(chunklen(ck) != len)
		return 0;
	return !memcmp(ck->start, str, len);
}

static int chunkis(struct chunk* ck, const char* str)
{
	return chunkeq(ck, str, strlen(str));
}

int find_line(struct line* ln, int i, char* val, int len)
{
	int lk;
	int n = i + 1;
	struct chunk ck[n];

	for(lk = firstline(ln); lk; lk = nextline(ln)) {
		if(split_line(ln, ck, n) < n)
			continue;
		if(!chunkeq(&ck[i], val, len))
			continue;
		return 0;
	}

	return -ENOENT;
}

int find_mac(struct line* ln, byte mac[6])
{
	FMTBUF(p, e, buf, 3*6+2);
	p = fmtmac(p, e, mac);
	FMTEND(p, e);

	return find_line(ln, 0, buf, p - buf);
}

static void change_part(char* start, char* end, char* buf, int len)
{
	long offs = start - config;
	long offe = end - config;

	int oldlen = offe - offs;
	int newlen = len;

	int shift = newlen - oldlen;
	int shlen = config + datalen - end;

	if(extend_config(shift))
		return;

	char* head = config + offs;
	char* tail = config + offe;

	memmove(tail + shift, tail, shlen);
	memcpy(head, buf, len);
}

static void insert_line(char* buf, int len)
{
	char* at = config + datalen;

	if(extend_config(len + 1))
		return;

	memcpy(at, buf, len);
	at[len] = '\n';
}

void change_chunk(struct chunk* ck, char* str)
{
	change_part(ck->start, ck->end, str, strlen(str));

	ck->start = NULL;
	ck->end = NULL;

	modified = 1;
}

void drop_line(struct line* ln)
{
	if(!ln->start)
		return;

	int shift = -(ln->end - ln->start + 1);
	int shlen = config + datalen - ln->end - 1;

	if(extend_config(shift))
		return;

	memmove(ln->start, ln->end + 1, shlen);

	ln->start = NULL;
	ln->end = NULL;

	modified = 1;
}

void save_line(struct line* ln, char* buf, int len)
{
	if(ln->start)
		change_part(ln->start, ln->end, buf, len);
	else
		insert_line(buf, len);

	ln->start = NULL;
	ln->end = NULL;

	modified = 1;
}

void load_link(struct link* ls)
{
	struct chunk ck[2];
	struct line ln;

	if(load_config()) return;

	find_mac(&ln, ls->mac);

	if(split_line(&ln, ck, 2) < 2)
		return;

	struct chunk* md = &ck[1];

	if(chunkis(md, "down"))
		ls->mode = LM_DOWN;
	else if(chunkis(md, "dhcp"))
		ls->mode = LM_DHCP;
	else if(chunkis(md, "wifi"))
		ls->mode = LM_WIFI;
	//else if(chunkis(md, "static"))
	//	ls->mode = LM_SETIP;

	tracef("load %s mode %i\n", ls->name, ls->mode);
}

void save_link(struct link* ls, char* conf)
{
	struct line ln;

	if(load_config())
		return;

	find_mac(&ln, ls->mac);

	if(!conf) {
		drop_line(&ln);
	} else {
		FMTBUF(p, e, buf, 100);
		p = fmtmac(p, e, ls->mac);
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, conf);
		FMTEND(p, e);

		save_line(&ln, buf, p - buf);
	}
}
