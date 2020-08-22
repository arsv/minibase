/* Edge-triggering global system actions may be a bit too sensitive
   in some cases, like an accidental Power button click starting
   a system shutdown. To mend this, allow timed events: the action
   gets triggered once the key has been held for some time.

   The default time is chosen so that it's long enough to distinguish
   it from a simple keypress, and short enough to not interact with
   possible firmware actions (hold Power for ~3s to cut power).

   There's also at least one case where a long timeout is preferable:
   triggering sleep if the lid is kept closed for some amount of time.
   One second is way too short in this case, for let's make it 10 seconds.

   Making either constant configuration likely makes little sense, not
   nearly enough to justify the complexity. */

#include <bits/time.h>

#define HOLDTIME   1 /* sec; for keys */
#define LONGTIME  10 /* sec; for switches */

#define FDX 2

#define NDEVS 30
#define NPFDS (FDX + NDEVS)
#define ACLEN 1024

#define CODE_SWITCH (1<<15)

#define MODE_CTRL   (1<<0)
#define MODE_ALT    (1<<1)
#define MODE_HOLD   (1<<2)
#define MODE_LONG   (1<<3)

#define KEYM_LCTRL   (1<<4)
#define KEYM_RCTRL   (1<<5)
#define KEYM_LALT    (1<<6)
#define KEYM_RALT    (1<<7)

struct act {
	short len;
	short mode;
	short code;
	int pid;
	char cmd[];
};

struct top {
	char** environ;

	int npfds;
	int aclen;
	int nbits;
	byte* bits; /* byte[NPFDS] */
	void* pfds; /* struct pollfd[NPFDS] */
	void* acts; /* [ act, act, ... ] */

	struct act* held;
	struct timespec ts;

	int modstate;

	int dfd;
};

#define CTX struct top* ctx

void load_config(CTX);
void handle_inotify(CTX, int fd);
void handle_input(CTX, int fd, byte* mods);
void scan_devices(CTX);
int try_event_dev(CTX, int fd);
void check_children(CTX);

void set_static_fd(CTX, int i, int fd);
void set_device_fd(CTX, int i, int fd);
int find_device_slot(CTX);

int find_key(char* name, int nlen);
void hold_timeout(CTX, struct act* ka);

struct act* first(CTX);
struct act* next(CTX, struct act* at);
