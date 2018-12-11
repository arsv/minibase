#define NLINKS 10
#define NCONNS 10
#define NPROCS 10
#define NMARKS 50

#define TAGLEN 16
#define IFNLEN 16

/* link.flags, state-tracking */
#define LF_CARRIER  (1<<0)  /* Carrier present (IFF_RUNNING) */
#define LF_SETUP    (1<<1)  /* Setup script is running */
#define LF_REQUEST  (1<<2)  /* DHCP request script is running */
#define LF_DISCONT  (1<<3)  /* DHCP discontinuous mode */
#define LF_ERROR    (1<<4)  /* Setup script failed */
#define LF_MISNAMED (1<<5)  /* Last reported name does not match link.name */
/* user-controllable */
#define LF_STOP     (1<<6)
#define LF_DHCP     (1<<7)
#define LF_ONCE     (1<<8)

#define LN_SETUP    (1<<0)
#define LN_REQUEST  (1<<1)
#define LN_RENEW    (1<<2)
#define LN_CANCEL   (1<<3)

struct link {
	int ifi;
	uint seq;
	short flags;
	short needs;
	char name[IFNLEN];
	char mode[TAGLEN];
};

struct conn {
	int fd;
	int ifi;
	int rep;
};

struct proc {
	int pid;
	int ifi;
};

#define LS struct link* ls __unused

extern char** environ;
extern int netlink;
extern int pollset;

extern struct link links[];
extern struct conn conns[];
extern struct proc procs[];
extern int nprocs;
extern int nconns;
extern int nlinks;
extern int ctrlfd;

void quit(const char* msg, char* arg, int err) noreturn;

void accept_ctrl(int fd);
void handle_conn(struct conn* cn);
void unlink_ctrl(void);
void setup_ctrl(void);

void setup_rtnl(void);
void handle_rtnl(void);
void got_sigchld(void);

struct link* grab_link_slot(void);
struct link* find_link_slot(int ifi);
void free_link_slot(struct link* ls);

struct proc* grab_proc_slot(void);
struct proc* find_proc_slot(int pid);
void free_proc_slot(struct proc* ch);

struct conn* grab_conn_slot(void);
void free_conn_slot(struct conn* cn);

int any_procs_left(void);
int any_procs_running(LS);
void kill_all_procs(LS);
int kill_tagged(LS, int tag);
void stop_link_procs(struct link* ls, int drop);

void report_done(LS);

void set_timeout(int sec);
void timer_expired(void);

int check_marked(int ifi);
void unmark_link(int ifi);

void spawn_identify(int ifi, char* name);
int assess_link(LS);
void reassess_link(LS);

void request_link_name(LS);
