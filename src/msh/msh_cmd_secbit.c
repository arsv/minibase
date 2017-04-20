#include <bits/secure.h>
#include <sys/prctl.h>

#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct secbit {
	char name[16];
	short bit;
} secbits[] = {
	{ "keepcaps", SECURE_KEEP_CAPS            },
	{ "nosetuid", SECURE_NO_SETUID_FIXUP      },
	{ "noroot",   SECURE_NOROOT               },
	{ "noraise",  SECURE_NO_CAP_AMBIENT_RAISE },
	{ "",         0                           }
};

int cmd_secbits(struct sh* ctx, int argc, char** argv)
{
	int ret;
	const struct secbit* sb;
	int bits = 0;
	int i;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	for(i = 1; i < argc; i++) {
		char* arg = argv[i];
		int arglen = strlen(arg);
		int lock = 0;

		if(arglen > 5 && !strncmp(arg + arglen - 5, "-lock", 5)) {
			lock = 1;
			arg[arglen + arglen - 5] = '\0';
		}

		for(sb = secbits; sb->name[0]; sb++)
			if(!strncmp(sb->name, arg, sizeof(sb->name)))
				break;
		if(!sb->name[0])
			return error(ctx, "unknown bit", arg, 0);

		bits |= (1 << sb->bit);
		if(!lock) continue;
		bits |= (1 << (sb->bit + 1));
	}
	
	return fchk(sys_prctl(PR_SET_SECUREBITS, bits, 0, 0, 0),
			ctx, "prctl", "PR_SET_SECUREBITS");
}
