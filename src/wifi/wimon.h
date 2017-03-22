#include <bits/ints.h>

#define NLINKS 8
#define NSCANS 64

#define NAMELEN 16
#define SSIDLEN 32
#define PSKLEN 32

/* link.flags */
#define F_WIFI     (1<<0)
#define F_SCANNING (1<<1)
#define F_SCANRES  (1<<2)
#define F_CONNECT  (1<<5)
#define F_CARRIER  (1<<6)
#define F_IPADDR   (1<<7)
#define F_GATEWAY  (1<<8)

/* scan.flags */
#define S_WPA      (1<<0)

struct link {
	int ifi;
	int seq;
	char name[NAMELEN];
	short flags;
	uint8_t bssid[6];
	uint8_t ip[4];
	uint8_t mask;
	uint8_t _;
};

struct scan {
	int ifi;
	int seq;
	int freq;
	int signal;
	int flags;
	uint8_t bssid[6];
	char ssid[SSIDLEN];
};

struct gate {
	int ifi;
	uint8_t ip[4];
};

extern struct link links[];
extern struct scan scans[];
extern int nlinks;
extern int nscans;
extern struct gate gateway;

struct netlink;
struct nlmsg;
struct nlgen;

extern char** environ;

extern struct netlink rtnl;
extern struct netlink genl;
extern int nl80211;
extern int ctrlfd;

void setup_signals(void);
void setup_pollfds(void);

void setup_rtnl(void);
void setup_genl(void);
void setup_ctrl(void);
void unlink_ctrl(void);

void handle_rtnl(struct nlmsg* msg);
void handle_genl(struct nlmsg* msg);
void accept_ctrl(int sfd);

void mainloop(void);
void waitpids(void);

struct link* find_link_slot(int ifi);
struct link* grab_link_slot(int ifi);
void free_link_slot(struct link* ls);

struct scan* grab_scan_slot(int ifi, uint8_t* bssid);
void drop_stale_scan_slots(int ifi, int seq);
void drop_scan_slots_for(int ifi);

void parse_scan_result(struct link* ls, struct nlgen* msg);

void add_addr(int ifi, uint8_t ip[4], int mask);
void del_addr(int ifi, uint8_t ip[4]);
