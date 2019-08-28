#include <cdefs.h>

#define NLINKS 16
#define NCONNS 8

#define TAGLEN 16
#define IFNLEN 16

#define LS_MASK         0x0F
#define LS_IDEF         0x00
#define LS_MODE         0x01
#define LS_STOP         0x02
#define LS_DHCP         0x03

#define LF_ENABLED     (1<<4)
#define LF_CARRIER     (1<<5)
#define LF_MARKED      (1<<6)
#define LF_AUTO_DHCP   (1<<7)
#define LF_DHCP_ONCE   (1<<8)

#define LF_RUNNING    (1<<10)
#define LF_STATUS     (1<<11)
#define LF_FAILED     (1<<12)

#define LF_NEED_MODE  (1<<16)
#define LF_NEED_STOP  (1<<17)
#define LF_NEED_DHCP  (1<<18)

struct link {
	int ifi;
	int pid;
	int flags;
	char name[IFNLEN];
	char mode[TAGLEN];
};

struct conn {
	int fd;
	int ifi;
};

struct top {
	int ctrlfd;
	int rtnlfd;
	int signalfd;

	char** environ;
	int nlinks;
	int nconns;
	struct link links[NLINKS];
	struct conn conns[NCONNS];
};

#define CTX struct top* ctx __unused
#define LS struct link* ls __unused

void quit(const char* msg, char* arg, int err) noreturn;

void handle_conn(CTX, struct conn* cn);
void close_conn(CTX, struct conn* cn);

void setup_netlink(CTX);
void handle_rtnl(CTX);
void got_sigchld(CTX);

struct link* grab_link_slot(CTX);
struct link* find_link_slot(CTX, int ifi);
void free_link_slot(CTX, struct link* ls);

int update_link_name(CTX, LS);
void spawn_identify(CTX, LS);

void setup_control(CTX);
void accept_ctrl(CTX);
void check_links(CTX);
void simulate_reconnect(CTX, LS);

void report_mode_errno(CTX, LS, int err);
void report_mode_exit(CTX, LS, int status);
void report_stop_errno(CTX, LS, int err);
void report_stop_exit(CTX, LS, int status);

void sighup_running_dhcp(LS);
