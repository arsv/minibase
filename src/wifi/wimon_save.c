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
#include "wimon_save.h"

/* Mini text editor for the config file. The config looks something like this:

	dev enp0s31f6 manual 192.168.1.3/24
	psk 001122...EEFF 1 Blackhole
	psk 91234A...47AC 0 publicnet
	psk F419BE...01F5 0 someothernet

   and wimon needs to add/remove/modify lines whenever device or AP settings
   change. The data gets read into memory on demand, queried (several times,
   in particular when sorting APs in link_scan_done), modified in memory if
   necessary, and synced back to disk once it's clear no queries will come
   any time soon. Defaults are generally not saved. */

#define PAGE 4096
#define MAX_CONFIG_SIZE 64*1024

char* config;
int blocklen;
int datalen;

struct line {
	char* start;
	char* end;
};

struct chunk {
	char* start;
	char* end;
};

static int pagealign(long size)
{
	return size + (PAGE - size % PAGE) % PAGE;
}

void preload_config(void)
{
	int fd;
	long ret;
	struct stat st;

	eprintf("%s\n", __FUNCTION__);

	if(config) return;

	if((fd = sysopen(WICFG, O_RDONLY)) < 0)
		return;
	if((ret = sysfstat(fd, &st)) < 0)
		goto out;
	if(st.st_size > MAX_CONFIG_SIZE) {
		ret = -E2BIG;
		goto out;
	}

	int size = pagealign(st.st_size + 1024);
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE;
	ret = sysmmap(NULL, size, prot, flags, fd, 0);

	if(MMAPERROR(ret))
		goto out;

	config = (char*)ret;
	blocklen = size;
	datalen = st.st_size;
	ret = 0;

	eprintf("got config of size %i\n", datalen);

out:	sysclose(fd);

	if(ret && ret != -ENOENT)
		warn(NULL, WICFG, ret);
}

void release_config(void)
{
	int fd;

	eprintf("%s\n", __FUNCTION__);

	if(!config)
		return;

	if((fd = sysopen3(WICFG, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
		warn("cannot open", WICFG, fd);
	} else {
		writeall(fd, config, datalen);
		sysclose(fd);
	}

	sysmunmap(config, blocklen);
	config = NULL;
}

static int setline(struct line* ln, char* p)
{
	char* confend = config + datalen;

	if(p >= confend)
		p = NULL;
	else if(p < config)
		p = NULL;

	ln->start = p ? p : NULL;
	ln->end = p ? strecbrk(p, confend, '\n') : NULL;

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

static int isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n');
}

static int prefixed(struct line* ln, char* pref, int len)
{
	if(!ln->start)
		return 0;
	if(ln->end - ln->start < len + 1)
		return 0;
	if(memcmp(ln->start, pref, len))
		return 0;
	if(!isspace(ln->start[len]))
		return 0;
	return 1;
}

static int splitline(struct line* ln, struct chunk* ck, int nc)
{
	char* end = ln->end;
	char* p = ln->start;
	int i;

	while(p < end && isspace(*p)) p++;

	while(p < end && i < nc) {
		ck[i].start = p;
		while(p < end && !isspace(*p)) p++;
		ck[i].end = p;
		i++;
		while(p < end && isspace(*p)) p++;
	}

	return i;
}

static int chunklen(struct chunk* ck)
{
	return ck->end - ck->start;
}

static int chunkeq(struct chunk* ck, char* str, int len)
{
	if(chunklen(ck) != len)
		return 0;
	return !memcmp(ck->start, str, len);
}

static int findline(struct line* ln, struct chunk* ck, int n,
		char* pref, int i, char* val)
{
	int ok;
	int plen = strlen(pref);
	int vlen = strlen(val);

	for(ok = firstline(ln); ok; ok = nextline(ln)) {
		if(!prefixed(ln, pref, plen))
			continue;
		if(splitline(ln, ck, n) < n)
			continue;
		if(!chunkeq(&ck[i], val, vlen))
			continue;
		return 1;
	}

	return 0;
}

int load_link(struct link* ls)
{
	struct line ln;
	struct chunk ck[3];

	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(!config) {
		warn("no config to load from", NULL, 0);
		return 0;
	}

	if(!findline(&ln, ck, 3, "dev", 1, ls->name))
		return 0;

	eprintf("load %s\n", ls->name);

	return 0;
}

void save_link(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(!config) {
		warn("no config to save to", NULL, 0);
		return;
	}
}

static void prep_ssid(char* buf, int len, uint8_t* ssid, int slen)
{
	char* p = buf;
	char* e = buf + len - 1;
	int i;

	for(i = 0; i < slen; i++) {
		if(ssid[i] == '\\') {
			p = fmtchar(p, e, '\\');
			p = fmtchar(p, e, '\\');
		} if(ssid[i] <= 0x20) {
			p = fmtchar(p, e, '\\');
			p = fmtbyte(p, e, ssid[i]);
		} else {
			p = fmtchar(p, e, ssid[i]);
		}
	}

	*p = '\0';
}

int saved_psk_prio(uint8_t* ssid, int slen)
{
	struct line ln;
	struct chunk ck[4];

	char ssidstr[3*32+4];
	prep_ssid(ssidstr, sizeof(ssidstr), ssid, slen);

	char* p;
	int prio;

	if(!findline(&ln, ck, 4, "psk", 3, ssidstr))
		return -1;

	if(!(p = parseint(ck[2].start, &prio)))
		return -1;
	if(p && !isspace(*p))
		return -1;

	return prio;
}

int load_psk(uint8_t* ssid, int slen, char* psk, int plen)
{
	struct line ln;
	struct chunk ck[4];

	char ssidstr[3*32+4];
	prep_ssid(ssidstr, sizeof(ssidstr), ssid, slen);

	if(!findline(&ln, ck, 4, "psk", 3, ssidstr))
		return 0;

	struct chunk* cpsk = &ck[1];
	int clen = chunklen(cpsk);

	if(plen < clen + 1)
		return 0;

	memcpy(psk, cpsk->start, clen);
	psk[clen] = '\0';

	return 1;
}

void save_psk(uint8_t* ssid, int slen, char* psk, int plen)
{
	eprintf("%s %s\n", __FUNCTION__, ssid);
}
