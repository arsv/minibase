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

/* for set_link_operstate; from linux/if.h, ref. RFC 2863 */
#define IF_OPER_DOWN   2
#define IF_OPER_UP     6

/* wifi.mode */
#define WM_UNDECIDED   0
#define WM_DISABLED    1
#define WM_ROAMING     2
#define WM_FIXEDAP     3
/* wifi.state */
#define WS_NONE        0
#define WS_TUNED       1
#define WS_CONNECTED   2
#define WS_RETRYING    3
/* wifi.flags */
#define WF_UNSAVED     (1<<0)

/* latch.evt */
#define LA_NONE        0
#define LA_WIFI_CONF   1
#define LA_WIFI_SCAN   2
#define LA_LINK_CONF   4
#define LA_LINK_DOWN   5

/* uplink.ifset */
#define ANYWIFI       -1

/* Netdev state tracking, and some low key per-device configuration.
   One of these is kept for each non-loopback kernel device. */

struct link {
	int ifi;
	int seq;
	char name[NAMELEN+2];
	short flags;

	uint8_t ip[4];
	uint8_t mask;

	uint8_t scan;
	uint8_t mode;
};

/* Persistent scan list entry. Wimon caches short-lived scan results
   for later usage. */

struct scan {
	int ifi;
	short freq;
	short signal;
	short type;
	short prio;
	short tries;
	short flags;
	uint8_t bssid[6];
	short slen;
	uint8_t ssid[SSIDLEN];
};

/* Child process tracking. Each child is bound to a specific link. */

struct child {
	int ifi;
	int pid;
};

/* Primary gateway control and tracking. This is mostly to tell whether
   wimon should stop one interface before attempting to start another. */

#define UL_NONE  0
#define UL_DOWN -1
#define UL_WIFI -2
/* and anything positive means fixed uplink ifi */

struct uplink {
	int mode;
	int ifi;
	short routed;
	uint8_t gw[4];
};

/* Wireless config automation. Singular and generally *not* bound
   to a specific device. */

struct wifi {
	short mode;
	short state;
	short flags;
	int ifi;
	short freq;
	short prio;
	short slen;
	short type;
	uint8_t ssid[SSIDLEN];
	uint8_t bssid[6];
	char psk[2*32+1];
};

struct latch {
	int evt;
	int ifi;
	int cfd;
};

extern struct link links[];
extern struct scan scans[];
extern int nlinks;
extern int nscans;
extern struct gate gateway;
extern struct child children[];
extern int nchildren;

extern struct uplink uplink;
extern struct wifi wifi;
extern struct latch latch;

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

void scan_all_wifis(void);
void trigger_scan(struct link* ls, int freq);
void trigger_disconnect(int ifi);
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

void gate_open(int ifi, uint8_t gw[4]);
void gate_lost(int ifi, uint8_t gw[4]);

/* wimon_proc.c */

void link_deconfed(struct link* ls);

void spawn_dhcp(struct link* ls, char* opts);
void spawn_wpa(struct link* ls, char* mode);

void configure_link(struct link* ls);
void terminate_link(struct link* ls);
void drop_link_procs(struct link* ls);

/* wimon_save.c */

int load_link(struct link* ls);
void save_link(struct link* ls);

int saved_psk_prio(uint8_t* ssid, int slen);

int load_psk(uint8_t* ssid, int slen, char* psk, int plen);
void save_psk(uint8_t* ssid, int slen, char* psk, int plen);

int setlatch(int evt, struct link* ls, int cfd);
void unlatch(int evt, struct link* ls, int err);
int any_ongoing_scans(void);
int any_active_wifis(void);
