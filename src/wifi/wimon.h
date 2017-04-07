#include <bits/ints.h>

#define NLINKS 8
#define NSCANS 64
#define NCHILDREN 10
#define NTASKS 4

#define NAMELEN 16
#define SSIDLEN 32
#define PSKLEN 32

/* link.state */
#define S_ENABLED  (1<<0)
#define S_WIRELESS (1<<1)
#define S_CONNECT  (1<<2)
#define S_CARRIER  (1<<3)
#define S_IPADDR   (1<<4)

/* link.scan */
#define SC_NONE    0
#define SC_REQUEST 1
#define SC_ONGOING 2
#define SC_RESULTS 3
#define SC_DUMPING 4

/* link.mode */
#define LM_NOTOUCH (1<<0)
#define LM_MANUAL  (1<<1) /* do not run dhcp */
#define LM_LOCAL   (1<<2) /* run dhpc in local mode only */
#define LM_NOWIFI  (1<<3) /* do not use for wifi autoscans */
#define LM_SCANRQ  (1<<4) /* scan requested (ls->scan is not spontaneous) */
#define LM_CCHECK  (1<<5) /* carrier check requested */
#define LM_TERMRQ  (1<<6) /* terminate_link called */

/* scan.type */
#define ST_WPS         (1<<0)
#define ST_WPA         (1<<1)
#define ST_RSN         (1<<3)
#define ST_RSN_PSK     (1<<4)
#define ST_RSN_P_TKIP  (1<<5) /* pairwise */
#define ST_RSN_P_CCMP  (1<<6)
#define ST_RSN_G_TKIP  (1<<7) /* group */
#define ST_RSN_G_CCMP  (1<<8)

/* for set_link_operstate; from linux/if.h, ref. RFC 2863 */
#define IF_OPER_DOWN   2
#define IF_OPER_UP     6

struct link {
	int ifi;
	int seq;
	char name[NAMELEN+2];
	short flags;

	uint8_t bssid[6];
	uint8_t ip[4];
	uint8_t mask;

	uint8_t scan;
	uint8_t mode;
};

struct scan {
	int ifi;
	int freq;
	int signal;
	int type;
	short seen;
	uint8_t bssid[6];
	uint8_t ssid[SSIDLEN];
};

struct gate {
	int ifi;
	uint8_t ip[4];
};

struct child {
	int ifi;
	int pid;
};

extern struct link links[];
extern struct scan scans[];
extern int nlinks;
extern int nscans;
extern struct gate gateway;
extern struct child children[];
extern int nchildren;

struct netlink;
struct nlmsg;
struct nlgen;

extern char** environ;
extern int envcount;

extern struct netlink rtnl;
extern struct netlink genl;
extern int nl80211;
extern int ctrlfd;

void setup_rtnl(void);
void setup_genl(void);
void setup_ctrl(void);
void accept_ctrl(int sfd);
void unlink_ctrl(void);
void handle_rtnl(struct nlmsg* msg);
void handle_genl(struct nlmsg* msg);
void set_link_operstate(int ifi, int operstate);
void del_link_addresses(int ifi);
void waitpids(void);
void schedule(void (*call)(void), int secs);

void trigger_scan(struct link* ls);
void parse_scan_result(struct link* ls, struct nlgen* msg);

int ssidlen(uint8_t* ssid);
