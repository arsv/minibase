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

#define HOLDTIME   1000 /* 1s in ms; for keys */
#define LONGTIME  10000 /* 10s in ms; for switches */

#define NDEVICES 40
#define NACTIONS 40

#define CODE_SWITCH (1<<15)

#define MODE_CTRL   (1<<0)
#define MODE_ALT    (1<<1)
#define MODE_HOLD   (1<<2)
#define MODE_LONG   (1<<3)

#define MOD_LCTRL   (1<<4)
#define MOD_RCTRL   (1<<5)
#define MOD_LALT    (1<<6)
#define MOD_RALT    (1<<7)

struct device {
	int fd;
	int minor;
	int mods;
};

struct action {
	short mode;
	short code;
	char cmd[20];
	char arg[8];
	int time;
	int minor;
};

extern char** environ;

extern struct device devices[];
extern struct action actions[];
extern int ndevices;
extern int nactions;

extern int inotifyfd;
extern int pollready;

void load_config(void);
void handle_inotify(int fd);
void handle_input(struct device* kb, int fd);
void poll_inputs(void);
void setup_signals(void);
void setup_devices(void);
int try_event_dev(int fd);

int find_key(char* name);
void hold_done(struct action* ka);
