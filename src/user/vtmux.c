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
int inotifyfd;

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
	REPORT(ENOSYS), REPORT(EIO), REPORT(ENOTSOCK),
	RESTASNUMBERS
};

#define OPTS "gn"
#define OPT_g (1<<0)
#define OPT_n (1<<1)

/* Pinned clients are never started in background. We choose one
   to run in foreground, and leave the rest to be start on the first
   switch to their respective ttys. Background startup implies no KMS
   master, so no way to figure out things like display resolution.
   Some clients might be able to cope with that, some might not.

   Also it is not clear how background startup may be useful.

   If there is anything other than greeter to start, it is likely
   some kind of auto-login setup and we should drop the user there.
   The first non-greeter client is always consoles[1]. Otherwise,
   start greeter from consoles[0] and let it spawn sessions. */

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
	setup_pinned(greeter, argc - i, argv + i, spareinitial);
	setup_keyboards();

	if(consoles[1].pin)
		invoke(&consoles[1]);
	else
		invoke(&consoles[0]);

	mainloop();

	shutdown();

	return 0;
}
