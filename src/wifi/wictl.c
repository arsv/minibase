#include <bits/errno.h>

#include <nlusctl.h>
#include <heap.h>
#include <util.h>
#include <fail.h>

#include "config.h"
#include "common.h"
#include "wictl.h"

ERRTAG = "wictl";
ERRLIST = {
	REPORT(ENOENT), REPORT(EINVAL), REPORT(ENOSYS), REPORT(ENOENT),
	REPORT(EACCES), REPORT(EPERM), RESTASNUMBERS
};

#define OPTS "ewsipzn"
#define OPT_e (1<<0)
#define OPT_w (1<<1)
#define OPT_s (1<<2)
#define OPT_p (1<<4)
#define OPT_z (1<<5)

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	struct top ctx;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(opts)
		fail("unsupported opts", NULL, 0);

	top_init(&ctx);
	cmd_status(&ctx);

	return 0;
}
