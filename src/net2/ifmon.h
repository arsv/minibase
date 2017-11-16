#define NLINKS 10
#define NCONNS 10
#define NPROCS 10
#define NDHCPS 4
#define NAMELEN 16

#define LM_SKIP  0
#define LM_DOWN  1
#define LM_DHCP  2
#define LM_WIFI  3

#define LF_ENABLED  (1<<0)
#define LF_CARRIER  (1<<1)
#define LF_RUNNING  (1<<2)
#define LF_DHCPREQ  (1<<3)
#define LF_ADDRSET  (1<<4)
#define LF_FLUSHREQ (1<<5)
#define LF_FLUSHING (1<<6)
#define LF_STOP     (1<<7)
#define LF_STOPPING (1<<8)
#define LF_ERROR    (1<<9)
#define LF_DHCPFAIL (1<<10)
#define LF_UNSAVED  (1<<11)

#define CH_DHCP  1
#define CH_WIENC 2

#define DH_DISCOVER 1
#define DH_REQUEST  2
#define DH_LEASED   3
#define DH_RENEWING 4

struct link {
	int ifi;
	uint seq;
	int flags;
	short mode;
	byte mac[6];
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

struct dhcp {
	int ifi;
	int fd;
	int state;
	int tries;
	uint timer;
	uint extra;
	byte ourmac[6];
	byte srvmac[6];
	byte srvaddr[4];
	byte ouraddr[4];
	uint32_t xid;
};

#define LS struct link* ls __unused
#define DH struct dhcp* dh

extern char** environ;
extern int netlink;
extern int pollset;

extern struct proc procs[];
extern struct conn conns[];
extern struct link links[];
extern struct dhcp dhcps[];
extern int nprocs;
extern int nconns;
extern int nlinks;
extern int ndhcps;
extern int ctrlfd;

void quit(const char* msg, char* arg, int err) noreturn;

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
struct dhcp* grab_dhcp_slot(int ifi);
struct dhcp* find_dhcp_slot(int ifi);
void free_dhcp_slot(struct dhcp* dh);

void link_new(LS);
void link_enabled(LS);
void link_carrier(LS);
void link_lost(LS);
void link_down(LS);
void link_gone(LS);
void link_exit(LS, int tag, int status);
void link_flushed(LS);

void waitpids(void);

void load_link(LS);
void save_link(LS);
void save_flagged_links();

void enable_iface(LS);
void disable_iface(LS);
void delete_addr(LS);

int spawn(LS, int tag, char** argv);
int any_procs_left(LS);
void kill_all_procs(LS);
int kill_tagged(LS, int tag);
void stop_link_procs(struct link* ls, int drop);

int stop_link(LS);
void start_dhcp(LS);
void stop_dhcp(LS);

void report_link_down(LS);
void report_link_gone(LS);
void report_link_enabled(LS);
void report_link_carrier(LS);
void report_link_stopped(LS);
void report_dhcp_done(DH);
void report_dhcp_fail(DH);

void set_timeout(int sec);
void timer_expired(void);
void request_pollfds_update(void);

struct timespec;

void prep_dhcp_timeout(struct timespec* ts);
void update_dhcp_timers(struct timespec* dt);
void handle_dhcp(struct dhcp* dh);
void dhcp_error(struct dhcp* dh);

void set_iface_address(int ifi, uint8_t ip[4], int mask, int lt, int rt);
void add_default_route(int ifi, uint8_t gw[4]);
