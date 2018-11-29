#include <sys/file.h>
#include <sys/fpath.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "common.h"
#include "ifmon.h"

#define PAGE 4096
#define MAX_CONFIG_SIZE 64*1024

struct cfgrec {
	byte mac[6];
	char tag[IFNLEN];
};

static void read_load_whole(int fd, char* name, struct stat* st)
{
	uint recsize = sizeof(struct cfgrec);
	uint size = st->size;
	uint count = size / recsize;
	uint szmod = size % recsize;
	int ret;

	if(size != st->size || szmod != 0) {
		warn("invalid size of", name, 0);
		return;
	}
	if(count > NLINKS) {
		warn("too many entries in", name, 0);
		size = NLINKS*recsize;
	}

	char buf[size];

	if((ret = sys_read(fd, buf, size)) < 0) {
		warn("read", name, ret);
		return;
	} else if((uint)ret != size) {
		warn("incomplete read in", name, 0);
		return;
	}

	void* ptr = buf;
	void* end = buf + size;
	int i = 0;

	for(; ptr < end; ptr += recsize) {
		struct cfgrec* cr = ptr;
		struct link* ln = &links[i++];
		memcpy(ln->mac, cr->mac, MACLEN);
		memcpy(ln->tag, cr->tag, IFNLEN);
	}

	nlinks = i;
}

void load_link_db()
{
	int fd, ret;
	char* name = IFCFG;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0) {
		if(fd != -ENOENT)
			warn(NULL, name, fd);
		return;
	}
	if((ret = sys_fstat(fd, &st)) < 0) {
		warn("stat", name, ret);
		return;
	}

	read_load_whole(fd, name, &st);

	sys_close(fd);
}

static int anything_changed(void)
{
	struct link* ln = &links[0];
	struct link* le = ln + nlinks;

	while(ln < le)
		if(ln->flags & LF_UNSAVED)
			return 1;

	return 0;
}

static int count_tagged_links(void)
{
	struct link* ln = &links[0];
	struct link* le = ln + nlinks;
	int count = 0;

	while(ln < le)
		if(ln->tag[0])
			count++;

	return count;
}

static void put_tagged_links(void* buf, uint len)
{
	struct link* ln = &links[0];
	struct link* le = ln + nlinks;

	struct cfgrec* cr = buf;
	struct cfgrec* ce = buf + len;

	while(ln < le && cr + 1 <= ce) {
		if(!ln->tag[0])
			continue;

		memcpy(cr->mac, ln->mac, MACLEN);
		memcpy(cr->tag, ln->tag, TAGLEN);
	}
}

void save_link_db()
{
	int fd, ret;
	char* name = IFCFG;

	if(!anything_changed())
		return;

	uint count = count_tagged_links();

	if(!count) {
		sys_unlink(name);
		return;
	}

	uint recsize = sizeof(struct cfgrec);
	uint size = recsize*count;
	char buf[size];

	put_tagged_links(buf, size);

	if((fd = sys_open3(name, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0) {
		warn(NULL, name, fd);
		return;
	}

	if((ret = sys_write(fd, buf, size)) < 0) {
		warn(NULL, name, ret);
	} else if((uint)ret != size) {
		warn("incomplete write in", name, 0);
	}

	sys_close(fd);
}
