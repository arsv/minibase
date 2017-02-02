#include <bits/ioctl/tty.h>
#include <sys/open.h>
#include <sys/ppoll.h>
#include <sys/ioctl.h>
#include <sys/_exit.h>

#include <fail.h>

#include "vtmux.h"

ERRTAG = "vtmux";
ERRLIST = { RESTASNUMBERS };

char** environ;

int activetty;
char* greeter = "login";

struct vtx consoles[CONSOLES];
struct vtd vtdevices[INPUTS];
struct kbd keyboards[KEYBOARDS];

int nconsoles;
int nvtdevices;
int nkeyboards;

void setup_globals(void)
{
	struct vt_stat vst;
	long ret;

	if((ret = sysioctl(0, VT_LOCKSWITCH, 0)))
		fail("ioctl", "VT_LOCKSWITCH", ret);

	if((ret = sysioctl(0, VT_GETSTATE, (long)&vst)))
		fail("ioctl", "VT_GETSTATE", ret);

	activetty = vst.active;
}

int main(int argc, char** argv, char** envp)
{
	environ = envp;

	setup_globals();
	setup_greeter();
	setup_keyboards();
	spawn_greeter();

	mainloop();

	return 0;
}
