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

int cmd_secbits(struct sh* ctx)
{
	const struct secbit* sb;
	char* arg;
	int bits = 0;

	if(noneleft(ctx))
		return -1;
	while((arg = shift(ctx))) {
		int lock = striplock(arg);

		for(sb = secbits; sb->name[0]; sb++)
			if(!strncmp(sb->name, arg, sizeof(sb->name)))
				break;
		if(!sb->name[0])
			return error(ctx, "unknown bit", arg, 0);

		bits |= (1 << sb->bit);
		if(!lock) continue;
		bits |= (1 << (sb->bit + 1));
	}

	return fchk(sys_prctl(PR_SET_SECUREBITS, bits, 0, 0, 0), ctx, NULL);
}
