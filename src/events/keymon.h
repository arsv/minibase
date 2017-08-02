#define CMDLEN  50

#define NDEVICES 40
#define NACTIONS 40

#define MOD_CTRL    (1<<0)
#define MOD_ALT     (1<<1)
#define MOD_HOLD    (1<<2)

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
	char cmd[16];
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
