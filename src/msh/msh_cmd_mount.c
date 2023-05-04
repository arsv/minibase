#include <sys/mount.h>
#include <sys/fpath.h>

#include <string.h>
#include <util.h>
#include <main.h>

#include "msh.h"
#include "msh_cmd.h"

/* It might seem weird, but `mount` is a simple syscall that hardly
   justifies its own executable. It is also very likely that most
   standalone mount calls in a typical system will be performed
   from msh during system startup. */

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

static int mount_flags(CTX)
{
	char* optstr;

	if(!(optstr = dash_opts(ctx)))
		return 0;

	char c, *p;
	int flags = 0;
	const struct mflag* fp;

	for(p = optstr; (c = *p); p++) {
		for(fp = mflags; fp < ARRAY_END(mflags); fp++) {
			if(fp->opt != c)
				continue;

			flags |= (1 << fp->bit);
		}
	}

	return flags;
}

static void mount_rest(CTX, char* target, char* source, int flags)
{
	int ret;

	char* fstype;
	char* data;

	if(flags & (MS_MOVE | MS_REMOUNT))
		fstype = NULL;
	else if(got_more_arguments(ctx))
		fstype = shift(ctx);
	else
		fatal(ctx, "missing fstype", NULL);

	if(got_more_arguments(ctx))
		data = shift(ctx);
	else
		data = NULL;

	no_more_arguments(ctx);

	if((ret = sys_mount(source, target, fstype, flags, data)) < 0)
		error(ctx, "mount", NULL, ret);
}

void cmd_mount(CTX)
{
	char* target;
	char* source;

	int flags = mount_flags(ctx);

	if(got_more_arguments(ctx))
		target = shift(ctx);
	else
		fatal(ctx, "mountpoint required", NULL);

	if(flags & MS_REMOUNT)
		source = NULL;
	else if(got_more_arguments(ctx))
		source = shift(ctx);
	else
		fatal(ctx, "need device to mount", NULL);

	mount_rest(ctx, target, source, flags);
}

void cmd_vmount(CTX)
{
	char* target;

	int flags = mount_flags(ctx);

	if(got_more_arguments(ctx))
		target = shift(ctx);
	else
		fatal(ctx, "mountpoint required", NULL);

	if(flags & (MS_BIND | MS_MOVE | MS_REMOUNT))
		fatal(ctx, "invalid arguments", NULL);

	mount_rest(ctx, target, NULL, flags);
}
