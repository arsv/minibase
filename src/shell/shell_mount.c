#include <sys/file.h>
#include <sys/mount.h>
#include <string.h>

#include <format.h>
#include <printf.h>
#include <output.h>
#include <util.h>

#include "shell.h"

/* The syntax here follows msh mount, for compatibility and general sanity
   reasons. Most of this will likely be never used but the code is quite small
   so it makese some sense to keep it here just in case. */

static const struct mflag {
	char opt;
	byte bit;
} mflags[] = {
	{ 'r',  0 }, // MS_RDONLY
	{ 's',  1 }, // MS_NOSUID
	{ 'd',  2 }, // MS_NODEV
	{ 'x',  3 }, // MS_NOEXEC
	{ 'y',  4 }, // MS_SYNCHRONOUS
	{ 'e',  5 }, // MS_REMOUNT
	{ 'a', 10 }, // MS_NOATIME
	{ 'A', 11 }, // MS_NODIRATIME
	{ 'b', 12 }, // MS_BIND
	{ 'm', 13 }, // MS_MOVE
	{ 'i', 15 }, // MS_SILENT
	{ 'l', 25 }, // MS_LAZYTIME
};

static int mount_flags(void)
{
	char c, *p;
	int flags = 0;
	char* opts;
	const struct mflag* fp;

	if(!(opts = shift_opt()))
		return 0;

	for(p = opts; (c = *p); p++) {
		for(fp = mflags; fp < ARRAY_END(mflags); fp++) {
			if(fp->opt != c)
				continue;

			flags |= (1 << fp->bit);
		}
	}

	return flags;
}

static void mount_rest(char* target, char* source, int flags)
{
	int ret;

	char* fstype;
	char* data;

	if(flags & (MS_MOVE | MS_REMOUNT))
		fstype = NULL;
	else if(got_more_args())
		fstype = shift();
	else
		return repl("missing fstype", NULL, 0);

	if(got_more_args())
		data = shift();
	else
		data = NULL;

	if(extra_arguments())
		return;

	if((ret = sys_mount(source, target, fstype, flags, data)) < 0)
		repl(NULL, NULL, ret);
}

void cmd_mount(void)
{
	char* target = NULL;
	char* source = NULL;
	int flags;

	flags = mount_flags();

	if(!(target = shift()))
		return repl("mountpoint required", NULL, 0);

	if(flags & MS_REMOUNT)
		source = NULL;
	else if(got_more_args())
		source = shift();
	else
		return repl("need device to mount", NULL, 0);

	mount_rest(target, source, flags);
}

void cmd_umount(void)
{
	char* dir;
	int flags = 0;
	int ret;

	if(!(dir = shift_arg()))
		return;

	if((ret = sys_umount(dir, flags)) < 0)
		repl(NULL, NULL, ret);
}
