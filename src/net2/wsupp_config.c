#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "common.h"
#include "wsupp.h"

/* Mini text editor for the config file. The config looks something like this:

	001122...EEFF Blackhole
	91234A...47AC publicnet
	F419BE...01F5 someothernet

   and wsupp only uses it to store PSKs at this point.

   The data gets read into memory on demand, queried, modified in memory
   if necessary, and synced back to disk. */

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

	if((fd = sys_open3(WICFG, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
		warn("cannot open", WICFG, fd);
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

static int open_stat_config(int* size)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(WICFG, O_RDONLY)) < 0) {
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
		warn(NULL, WICFG, ret);

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

	if(config)
		return 0;

	if((fd = open_stat_config(&filesize)) < 0)
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

static int split_line(struct line* ln, struct chunk* ck, int nc)
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

static int chunklen(struct chunk* ck)
{
	return ck->end - ck->start;
}

static int chunkeq(struct chunk* ck, const char* str, int len)
{
	if(chunklen(ck) != len)
		return 0;
	return !memcmp(ck->start, str, len);
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

static char* fmt_ssid(char* p, char* e, byte* ssid, int slen)
{
	int i;

	for(i = 0; i < slen; i++) {
		if(ssid[i] == '\\') {
			p = fmtchar(p, e, '\\');
			p = fmtchar(p, e, '\\');
		} else if(ssid[i] == ' ') {
			p = fmtchar(p, e, '\\');
			p = fmtchar(p, e, ' ');
		} else if(ssid[i] <= 0x20) {
			p = fmtchar(p, e, '\\');
			p = fmtchar(p, e, 'x');
			p = fmtbyte(p, e, ssid[i]);
		} else {
			p = fmtchar(p, e, ssid[i]);
		}
	}

	return p;
}

static int find_ssid(struct line* ln, byte* ssid, int slen)
{
	FMTBUF(p, e, buf, 3*32+2);
	p = fmt_ssid(p, e, ssid, slen);
	FMTEND(p, e);

	/* 0 is psk, 1 is ssid */
	return find_line(ln, 1, buf, p - buf);
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

static void drop_line(struct line* ln)
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

int got_psk_for(byte* ssid, int slen)
{
	struct line ln;

	if(load_config())
		return 0;
	if(find_ssid(&ln, ssid, slen))
		return 0;

	return 1;
}

int load_psk(byte* ssid, int slen, byte psk[32])
{
	struct line ln;
	struct chunk ck[2];
	int ret = -ENOKEY;

	if(load_config())
		return ret;
	if(find_ssid(&ln, ssid, slen))
		return ret;
	if(split_line(&ln, ck, 2) < 2)
		return ret;

	struct chunk* cpsk = &ck[0];
	int clen = chunklen(cpsk);

	if(clen != 2*32)
		return -EINVAL;
	if(!parsebytes(cpsk->start, psk, 32))
		return -EINVAL;

	return 0;
}

void save_psk(byte* ssid, int slen, byte psk[32])
{
	struct line ln;

	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtbytes(p, e, psk, 32);
	p = fmtstr(p, e, " ");
	p = fmt_ssid(p, e, ssid, slen);

	if(load_config()) return;

	find_ssid(&ln, ssid, slen);
	save_line(&ln, buf, p - buf);
}

int drop_psk(byte* ssid, int slen)
{
	struct line ln;
	int ret;

	if((ret = load_config()) < 0)
		return ret;
	if((ret = find_ssid(&ln, ssid, slen)) < 0)
		return ret;

	drop_line(&ln);

	return 0;
}

/* Saving and loading current AP */

void load_state(void)
{
	int fd, rd, ret;
	char* name = WICAP;
	char buf[64];

	if((fd = sys_open(name, O_RDONLY)) < 0)
		return;
	if((rd = sys_read(fd, buf, sizeof(buf))) < 0)
		goto out;

	byte* ssid = (byte*)buf;
	int slen = rd;

	if((ret = set_fixed_saved(ssid, slen)) < 0)
		goto out;

	opermode = OP_ACTIVE;
	ap.fixed = 1;
out:
	sys_close(fd);
	sys_unlink(name);
}

void save_state(void)
{
	int fd;
	char* name = WICAP;
	char* buf = (char*)ap.ssid;
	int len = ap.slen;

	if(!ap.fixed)
		return;
	if((fd = sys_open3(name, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0)
		return;

	sys_write(fd, buf, len);
	sys_close(fd);
}
