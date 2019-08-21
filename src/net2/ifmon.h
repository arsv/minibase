#define NLINKS 10
#define NCONNS 10
#define NMARKS 50

#define TAGLEN 16
#define IFNLEN 16

#define LS_IDLE          0x00
#define LS_SPAWN_MODE    0x01
#define LS_SPAWN_STOP    0x02
#define LS_DROP          0x03

#define LS_SEND_DISCOVER 0x15
#define LS_SEND_ACK      0x16
#define LS_SEND_RENEW    0x17
#define LS_SEND_RELEASE  0x18

#define LS_ADD_IP        0x20
#define LS_ADD_ROUTE     0x21
#define LS_DEL_ROUTE     0x22
#define LS_DEL_IP        0x23

#define LS_SPAWN_GW      0x30
#define LS_SPAWN_DNS     0x31
#define LS_SPAWN_NTP     0x32

#define LF_CARRIER     (1<<0)
#define LF_TOUCHED     (1<<1)
#define LF_DHCP_AUTO   (1<<8)
#define LF_DHCP_ONCE   (1<<9)
#define LF_SHUTDOWN   (1<<10)

struct link {
	int ifi;
	int seq;
	int pid;
	int state;
	int flags;
	char name[IFNLEN];
	char mode[TAGLEN];
};

struct conn {
	int fd;
	int ifi;
	int rep;
};

struct dhcp {
	int ifi;
};

extern char** environ;
extern int rtnlfd;
extern int ctrlfd;
extern int sigfd;

extern struct link links[NLINKS];
extern struct conn conns[NCONNS];
extern int nconns;
extern int nlinks;

#define LS struct link* ls __unused

void quit(const char* msg, char* arg, int err) noreturn;

void handle_conn(struct conn* cn);

void setup_netlink(void);
void handle_rtnl(void);
void got_sigchld(void);

struct link* grab_link_slot(void);
struct link* find_link_slot(int ifi);
void free_link_slot(struct link* ls);

struct conn* grab_conn_slot(void);
void free_conn_slot(struct conn* cn);

void set_timeout(int sec);
void timer_expired(void);

int check_marked(int ifi);
int is_marked(int ifi);
void unmark_link(int ifi);

void spawn_identify(int ifi, char* name);
int spawn_mode(LS);
int spawn_stop(LS);

void request_link_name(LS);
int update_link_name(LS);

void report_done(LS);
void report_errno(LS, int err);
void report_exit(LS, int status);

void link_wait(LS, int state);
void link_next(LS, int state);
void setup_control(void);
void accept_ctrl(void);
void check_links(void);
void script_exit(LS, int status);
