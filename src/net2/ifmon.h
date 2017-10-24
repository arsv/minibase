#define NLINKS 10
#define NCONNS 10
#define NPROCS 10
#define NAMELEN 16

#define LM_SKIP  0
#define LM_DOWN  1
#define LM_AUTO  2
#define LM_SETIP 3
#define LM_WIENC 4
#define LM_ERROR 5

#define LW_DISABLED 0
#define LW_ENABLED  1
#define LW_CARRIER  2

#define LD_NEUTRAL  0
#define LD_RUNNING  1
#define LD_FINISHED 2
#define LD_FLUSHING 3
#define LD_STOPPING 4
#define LD_ST_FLUSH 5

#define LS_NEUTRAL  0
#define LS_RUNNING  1
#define LS_STOPPING 2

#define CH_DHCP  1
#define CH_OTHER 2

struct link {
	int ifi;
	byte mac[6];
	short mode;
	short wire;
	short dhcp;
	short dhreq;
	short state;
	char name[NAMELEN];
};

struct conn {
	int fd;
	int ifi;
	int rep;
};

struct proc {
	int pid;
	int ifi;
	int tag;
};

#define LS struct link* ls

extern char** environ;
extern int netlink;

extern struct proc procs[];
extern struct conn conns[];
extern int nprocs;
extern int nconns;
extern int ctrlfd;

void accept_ctrl(int fd);
void handle_conn(struct conn* cn);
void handle_rtnl(void);
void setup_rtnl(void);
void unlink_ctrl(void);
void setup_ctrl(void);

struct link* grab_link_slot(int ifi);
struct link* find_link_slot(int ifi);
void free_link_slot(LS);

struct proc* grab_proc_slot(void);
struct proc* find_proc_slot(int pid);
void free_proc_slot(struct proc* ch);

struct conn* grab_conn_slot(void);

void link_new(LS);
void link_enabled(LS);
void link_carrier(LS);
void link_lost(LS);
void link_down(LS);
void link_gone(LS);
void link_exit(LS, int tag, int status);

void waitpids(void);

void load_link(LS);
void save_link(LS, char* conf);
void load_link_conf(LS);

void enable_iface(LS);
void disable_iface(LS);

int spawn(LS, int tag, char** argv);
int any_procs_left(LS);
void kill_all_procs(LS);
int kill_tagged(LS, int tag);
void stop_link_procs(struct link* ls, int drop);

int stop_link(LS);
int is_neutral(LS);
void dhcp_link(LS);

void report_link_down(LS);
void report_link_dhcp(LS, int status);
void report_link_enabled(LS);
void report_link_carrier(LS);
