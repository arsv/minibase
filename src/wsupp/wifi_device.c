#include <bits/errno.h>
#include <sys/file.h>
#include <sys/dents.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "wifi.h"

/* If the service has no device assigned, the client should try to pick
   one for it to use. This is done by scanning all available netdevs in
   /sys, looking for those with phy80211 entry.

   The client only needs to send ifindex to the service. However, error
   reporting is much easier if the name is available as well, so we
   provide the name as well.

   TODO: warn about multiple available devices. */

static int is_wifi_device(char* dir, char* name)
{
	struct stat st;

	FMTBUF(p, e, path, 70);
	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, "/phy80211");
	FMTEND(p, e);

	return (sys_stat(path, &st) >= 0);
}

void find_wifi_device(char out[32])
{
	char* dir = "/sys/class/net";
	char buf[1024];
	int fd, rd;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		goto out;

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;

			if(de->reclen <= 0)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;

			int len = strlen(de->name);

			if(len > 32 - 1)
				continue;
			if(!is_wifi_device(dir, de->name))
				continue;

			memcpy(out, de->name, len + 1);
			return;
		}
	}
out:
	fail("no suitable wifi device found", NULL, 0);
}
