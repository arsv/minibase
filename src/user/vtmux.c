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
int ctlsockfd;
char* greeter = "login";

struct vtx consoles[CONSOLES];
struct vtd vtdevices[INPUTS];
struct kbd keyboards[KEYBOARDS];

int nconsoles;
int nvtdevices;
int nkeyboards;

int main(int argc, char** argv, char** envp)
{
	environ = envp;

	setup_greeter();
	setup_keyboards();
	spawn_greeter();

	mainloop();

	return 0;
}
