#include <bits/secure.h>
#include <sys/prctl.h>
#include <sys/seccomp.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct secbit {
	char name[12];
	int bit;
} secbits[] = {
	{ "keepcaps", SECURE_KEEP_CAPS            },
	{ "nosetuid", SECURE_NO_SETUID_FIXUP      },
	{ "noroot",   SECURE_NOROOT               },
	{ "noraise",  SECURE_NO_CAP_AMBIENT_RAISE }
};

static int striplock(char* str)
{
	int len = strlen(str);

	if(len <= 5)
		return 0;
	if(strncmp(str + len - 5, "-lock", 5))
		return 0;

	str[len-5] = '\0';
	return 1;
}

static void prctl_secbits(CTX)
{
	int lock, bits = 0;
	int cmd = PR_SET_SECUREBITS;
	const struct secbit* sb;
	char* arg;

	need_some_arguments(ctx);

	while((arg = next(ctx))) {
		lock = striplock(arg);

		for(sb = secbits; sb < ARRAY_END(secbits); sb++)
			if(!strncmp(sb->name, arg, sizeof(sb->name)))
				break;
		if(sb >= ARRAY_END(secbits))
			fatal(ctx, "unknown bit", arg);

		bits |= (1 << sb->bit);

		if(!lock) continue;

		bits |= (1 << (sb->bit + 1));
	}

	int ret = sys_prctl(cmd, bits, 0, 0, 0);

	check(ctx, "prctl", NULL, ret);
}

static void prctl_nonewprivs(CTX)
{
	int cmd = PR_SET_NO_NEW_PRIVS;

	no_more_arguments(ctx);

	int ret = sys_prctl(cmd, 1, 0, 0, 0);

	check(ctx, "prctl", NULL, ret);
}

static void prctl_seccomp(CTX)
{
	char* file = shift(ctx);

	no_more_arguments(ctx);

	int fd, ret;
	struct stat st;

	if((fd = sys_open(file, O_RDONLY | O_CLOEXEC)) < 0)
		error(ctx, NULL, file, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		error(ctx, "stat", file, ret);
	if(st.size > 0x7FFFFFFF) /* no larger-than-int files */
		error(ctx, NULL, file, -E2BIG);

	int proto = PROT_READ;
	int flags = MAP_PRIVATE;
	int size = st.size;

	if(!size || (size % 8))
		fatal(ctx, "odd size:", file);

	void* ptr = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(ptr)))
		error(ctx, "mmap", file, ret);

	int mode = SECCOMP_SET_MODE_FILTER;
	struct seccomp sc = {
		.len = size / 8,
		.buf = ptr
	};

	ret = sys_seccomp(mode, 0, &sc);

	check(ctx, "seccomp", file, ret);

	sys_munmap(ptr, size);
}

void cmd_prctl(CTX)
{
	char* arg = shift(ctx);

	if(!strcmp(arg, "no-new-privs"))
		return prctl_nonewprivs(ctx);
	if(!strcmp(arg, "secbits"))
		return prctl_secbits(ctx);
	if(!strcmp(arg, "seccomp"))
		return prctl_seccomp(ctx);

	fatal(ctx, "unknown prctl", arg);
}
