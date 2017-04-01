#include <bits/ints.h>

#define NLINKS 8
#define NSCANS 64
#define NCHILDREN 10

#define NAMELEN 16
#define SSIDLEN 32
#define PSKLEN 32

/* link.state */
#define S_ENABLED  (1<<0)
#define S_WIRELESS (1<<1)
//#define S_SCANNING (1<<2)
//#define S_SCANRES  (1<<3)
#define S_CONNECT  (1<<4)
#define S_CARRIER  (1<<5)
#define S_IPADDR   (1<<6)

/* link.scan */
#define SC_NONE    0
#define SC_REQUEST 1
#define SC_ONGOING 2
#define SC_RESULTS 3

/* link.mode2 */
#define M2_KEEP    0
#define M2_DOWN    1
#define M2_SCAN    2

/* link.mode3 */
#define M3_DHCP    0
#define M3_LOCAL   1
#define M3_FIXED   2

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
	char name[NAMELEN];

	uint8_t bssid[6];
	uint8_t ip[4];
	uint8_t mask;

	uint8_t state;
	uint8_t scan;
	uint8_t mode2;
	uint8_t mode3;
	uint8_t failed;
};

struct scan {
	int ifi;
	int freq;
	int signal;
	int type;
	uint8_t bssid[6];
	char ssid[SSIDLEN];
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
void flush_link_address(int ifi);
void waitpids(void);

void trigger_scan(struct link* ls);
void parse_scan_result(struct link* ls, struct nlgen* msg);
