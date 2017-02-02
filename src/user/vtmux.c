#include <bits/ioctl/tty.h>
#include <sys/open.h>
#include <sys/ppoll.h>
#include <sys/ioctl.h>
#include <sys/_exit.h>

#include <fail.h>

#include "vtmux.h"

ERRTAG = "vtmux";
ERRLIST = { RESTASNUMBERS };

int activetty;
char* greeter = "login";
char** environ;
int kmsonly;

struct vtx consoles[CONSOLES];
struct vtd vtdevices[INPUTS];
struct kbd keyboards[KEYBOARDS];

int nconsoles;
int nvtdevices;
int nkeyboards;

static void setup_base_tty(void)
{
	struct vt_stat vst;
	long ret;

	if((ret = sysioctl(0, VT_GETMODE, (long)&vst)))
		fail("ioctl", "VT_GETMODE", ret);

	activetty = vst.active;

	int fd = open_tty_device(activetty);

	if(fd < 0) _exit(0xFF);

	struct vtx* cvt = &consoles[0];

	cvt->tty = activetty;
	cvt->ttyfd = fd;

	nconsoles = 1;
}

int main(int argc, char** argv, char** envp)
{
	environ = envp;

	setup_base_tty();

	// setup_keyboards();

	spawn_greeter();

	mainloop();

	return 0;
}
