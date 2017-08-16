#include <bits/socket/netlink.h>
#include <sys/file.h>
#include <sys/dents.h>
#include <sys/socket.h>

#include <string.h>
#include <format.h>
#include <fail.h>

#include "findblk.h"

static void foreach_dir_in(char* dir, void (*func)(char*, char*), char* base);

static void check_part_ent(char* name, char* base)
{
	if(strncmp(name, base, strlen(base)))
		return;
	
	match_part(name);
}

static void check_dev_ent(char* name, char* _)
{
	if(!strncmp(name, "loop", 4))
		return;

	if(!match_dev(name))
		return;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/sys/block/");
	p = fmtstr(p, e, name);
	FMTEND(p);

	foreach_dir_in(path, check_part_ent, name);
}

static inline int dotddot(const char* p)
{
	if(!p[0])
		return 1;
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

static void foreach_dir_in(char* dir, void (*func)(char*, char*), char* base)
{
	int len = 1024;
	char buf[len];
	long fd, rd;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail("open", dir, fd);

	while((rd = sys_getdents(fd, buf, len)) > 0) {
		char* ptr = buf;
		char* end = buf + rd;
		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			switch(de->type) {
				case DT_UNKNOWN:
				case DT_DIR:
				case DT_LNK:
					break;
				default:
					continue;
			}

			func(de->name, base);
		}
	} if(rd < 0) {
		fail("getdents", dir, rd);
	}

	sys_close(fd);
}

void scan_devs(void)
{
	foreach_dir_in("/sys/block", check_dev_ent, NULL);
}
