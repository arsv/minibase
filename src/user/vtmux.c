#include <bits/ioctl/tty.h>
#include <sys/open.h>
#include <sys/ppoll.h>
#include <sys/ioctl.h>
#include <sys/_exit.h>

#include <util.h>
#include <fail.h>

#include "vtmux.h"

char** environ;
int activetty;
int initialtty;
int pollready;

struct vtx consoles[CONSOLES];
struct vtd vtdevices[INPUTS];
struct kbd keyboards[KEYBOARDS];

int nconsoles;
int nvtdevices;
int nkeyboards;

ERRTAG = "vtmux";
ERRLIST = {
	REPORT(EINVAL), REPORT(ENOENT), REPORT(ENOTDIR),
	REPORT(EFAULT), REPORT(ENOTTY), REPORT(EINTR),
	REPORT(ENOSYS), REPORT(EIO), RESTASNUMBERS
};

#define OPTS "gn"
#define OPT_g (1<<0)
#define OPT_n (1<<1)

int main(int argc, char** argv, char** envp)
{
	int i = 1;
	int opts = 0;
	char* greeter = "LOGIN";

	environ = envp;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc && opts & OPT_g)
		greeter = argv[i++];
	else if(opts & OPT_g)
		fail("missing argument", NULL, 0);

	int spareinitial = !!(opts & OPT_n);

	setup_signals();
	setup_fixed_vts(greeter, argc - i, argv + i, spareinitial);
	setup_keyboards();

	if(consoles[1].fix)
		spawn_fixed(&consoles[1]);
	else
		spawn_fixed(&consoles[0]);

	mainloop();

	return 0;
}
