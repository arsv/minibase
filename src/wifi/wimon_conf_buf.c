#include <sys/open.h>
#include <sys/read.h>
#include <sys/write.h>
#include <sys/close.h>
#include <sys/fstat.h>
#include <sys/mmap.h>
#include <sys/munmap.h>

#include <format.h>
#include <string.h>
#include <fail.h>
#include <util.h>

#include "config.h"
#include "wimon.h"

/* Mini text editor for the config file. The config looks something like this:

	dev enp0s31f6 manual 192.168.1.3/24
	psk 001122...EEFF 1 Blackhole
	psk 91234A...47AC 0 publicnet
	psk F419BE...01F5 0 someothernet

   and wimon needs to search/parse on certain events and add/remove/modify
   lines whenever device or AP settings change.

   The data gets read into memory on demand, queried, modified in memory
   if necessary, and synced back to disk.

   The events that cause config loading are often independent but sometimes
   have strong tendency to come in packs; think initial devices dump, or
   scan followed by auto-connect. To make things easier on the caller side,
   config is loaded on demand and unloading is schedule to happen several
   seconds later. This should be enough to avoid excessive load-save-load
   sequences. */

#define PAGE 4096
#define MAX_CONFIG_SIZE 64*1024

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

	if(!config)
		return;
	if(!modified)
		return;

	if((fd = sysopen3(WICFG, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
		warn("cannot open", WICFG, fd);
		return;
	}

	writeall(fd, config, datalen);
	sysclose(fd);
	modified = 0;
}

void drop_config(void)
{
	if(!config)
		return;
	if(modified)
		save_config();

	sysmunmap(config, blocklen);
	config = NULL;
	blocklen = 0;
	datalen = 0;
}

static int open_stat_config(int* size)
{
	int fd, ret;
	struct stat st;

	if((fd = sysopen(WICFG, O_RDONLY)) < 0) {
		*size = 0;
		return fd;
	}

	if((ret = sysfstat(fd, &st)) < 0)
		goto out;
	if(st.st_size > MAX_CONFIG_SIZE) {
		ret = -E2BIG;
		goto out;
	}

	*size = st.st_size;
	return fd;
out:
	sysclose(fd);

	if(ret && ret != -ENOENT)
		warn(NULL, WICFG, ret);

	return ret;
}

static int mmap_config_buf(int filesize)
{
	int size = pagealign(filesize + 1024);
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	long ret = sysmmap(NULL, size, prot, flags, -1, 0);

	if(MMAPERROR(ret))
		return ret;

	config = (char*)ret;
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
	if((ret = sysread(fd, config, filesize)) < filesize)
		return ret;

	return 0;
}

int load_config(void)
{
	int fd;
	long ret;
	int filesize;

	if(config)
		return 0;

	if((fd = open_stat_config(&filesize)) < 0)
		filesize = 0;
	if((ret = mmap_config_buf(filesize)) < 0)
		goto out;
	if((ret = read_config_whole(fd, filesize)) < 0)
		goto out;

	datalen = filesize;
	schedule(drop_config, 10);
	ret = 0;
out:	
	if(fd >= 0)
		sysclose(fd);
	if(ret && ret != -ENOENT)
		warn(NULL, WICFG, ret);

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

int chunkis(struct chunk* ck, const char* str)
{
	return chunkeq(ck, str, strlen(str));
}

int find_line(struct line* ln, char* pref, int i, char* val)
{
	int lk;
	int n = i + 1;
	struct chunk ck[n];
	int plen = strlen(pref);
	int vlen = val ? strlen(val) : 0;

	for(lk = firstline(ln); lk; lk = nextline(ln)) {
		if(split_line(ln, ck, n) < n)
			continue;
		if(!chunkeq(&ck[0], pref, plen))
			continue;
		if(val && !chunkeq(&ck[i], val, vlen))
			continue;
		return 0;
	}

	return -ENOENT;
}

static int extend_config(int len)
{
	if(len > 0 && datalen + len > blocklen)
		return -ENOMEM; /* XXX */

	datalen += len;

	return 0;
}

static void append_line(char* buf, int len)
{
	char* ptr = config + datalen;

	if(extend_config(len + 1))
		return;

	memcpy(ptr, buf, len);
	ptr[len] = '\n';
}

static void change_part(char* start, char* end, char* buf, int len)
{
	int oldlen = end - start;
	int newlen = len;

	int shift = newlen - oldlen;
	int shlen = config + datalen - end;

	if(extend_config(shift))
		return;

	memmove(end + shift, end, shlen);
	memcpy(start, buf, len);
}

void change_chunk(struct chunk* ck, char* str)
{
	change_part(ck->start, ck->end, str, strlen(str));
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

	modified = 1;
}

void save_line(struct line* ls, char* buf, int len)
{
	if(!ls->start)
		append_line(buf, len);
	else
		change_part(ls->start, ls->end, buf, len);

	modified = 1;
}
