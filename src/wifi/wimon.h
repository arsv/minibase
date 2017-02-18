#include <bits/ints.h>

#define NLINKS 8
#define NSCANS 64

struct link {
	int ifi;
	int seq;
	int wifi;
	char name[16];
};

struct scan {
	int ifi;
	int seq;
	int freq;
	int signal;
	int flags;
	uint8_t bssid[6];
	char ssid[32];
};

extern struct link links[];
extern struct scan scans[];
extern int nlinks;
extern int nscans;

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

void parse_scan_result(struct link* ls, struct nlgen* msg);
