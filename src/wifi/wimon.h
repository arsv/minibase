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
#define S_ACTIVE   (1<<2)
#define S_CONNECT  (1<<3)
#define S_CARRIER  (1<<4)
#define S_IPADDR   (1<<5)
#define S_TERMRQ   (1<<6)

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

/* scan.type */
#define ST_WPS         (1<<0)
#define ST_WPA         (1<<1)
#define ST_RSN         (1<<3)
#define ST_RSN_PSK     (1<<4)
#define ST_RSN_P_TKIP  (1<<5) /* pairwise */
#define ST_RSN_P_CCMP  (1<<6)
#define ST_RSN_G_TKIP  (1<<7) /* group */
#define ST_RSN_G_CCMP  (1<<8)

#define SF_SEEN        (1<<0)
#define SF_GOOD        (1<<1)
#define SF_TRIED       (1<<2)

/* for set_link_operstate; from linux/if.h, ref. RFC 2863 */
#define IF_OPER_DOWN   2
#define IF_OPER_UP     6

/* wifi.mode */
#define WM_NOSCAN    (1<<0)
#define WM_CONNECT   (1<<1)
#define WM_APLOCK    (1<<2)
#define WM_RETRY     (1<<3)
#define WM_UNSAVED   (1<<4)

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
	short freq;
	short signal;
	short type;
	short prio;
	short flags;
	uint8_t bssid[6];
	short slen;
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

struct wifi {
	int mode;
	int ifi;
	short freq;
	short prio;
	short slen;
	uint8_t ssid[SSIDLEN];
	char psk[2*32+1];
};

extern struct link links[];
extern struct scan scans[];
extern int nlinks;
extern int nscans;
extern struct gate gateway;
extern struct child children[];
extern int nchildren;

extern struct wifi wifi;

struct netlink;
struct nlmsg;
struct nlgen;

extern char** environ;
extern int envcount;

/* wimon_rtnl.c and wimon_genl.c */

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

void trigger_scan(struct link* ls, int freq);
void parse_scan_result(struct link* ls, struct nlgen* msg);

/* wimon_slot.c */

struct link* find_link_slot(int ifi);
struct link* grab_link_slot(int ifi);
void free_link_slot(struct link* ls);

struct scan* grab_scan_slot(uint8_t* bssid);
void drop_scan_slots(int ifi);
void free_scan_slot(struct scan* sc);

struct child* grab_child_slot(void);
struct child* find_child_slot(int pid);
void free_child_slot(struct child* ch);

/* wimon_link.c */

void link_new(struct link* ls);
void link_del(struct link* ls);
void link_wifi(struct link* ls);

void link_enabled(struct link* ls);
void link_carrier(struct link* ls);
void link_carrier_lost(struct link* ls);
void link_scan_done(struct link* ls);

void link_configured(struct link* ls);
void link_terminated(struct link* ls);

/* wimon_proc.c */

void link_deconfed(struct link* ls);

void spawn_dhcp(struct link* ls, char* opts);
void spawn_wpa(struct link* ls, struct scan* sc, char* mode, char* psk);

void terminate_link(struct link* ls);
void drop_link_procs(struct link* ls);

/* wimon_save.c */

int load_link(struct link* ls);
void save_link(struct link* ls);

int saved_psk_prio(uint8_t* ssid, int slen);

int load_psk(uint8_t* ssid, int slen, char* psk, int plen);
void save_psk(uint8_t* ssid, int slen, char* psk, int plen);
