#include <bits/ints.h>

#define NLINKS 8
#define NSCANS 64
#define NCHILDREN 10
#define NTASKS 4

#define NAMELEN 16
#define SSIDLEN 32
#define PSKLEN 32

/* Netdev state tracking, and some low key per-device configuration.
   One of these is kept for each non-loopback kernel device. */

/* link.flags */
#define S_NL80211  (1<<0)
#define S_ENABLED  (1<<1)
#define S_APLOCK   (1<<2)
#define S_CARRIER  (1<<3)
#define S_IPADDR   (1<<4)
#define S_UPLINK   (1<<5)

#define S_CHILDREN (1<<6)
#define S_STOPPING (1<<7)
#define S_SIGSENT  (1<<8)
#define S_UPCOMING (1<<9)

/* link.scan */
#define SC_NONE        0
#define SC_REQUEST     1
#define SC_ONGOING     2
#define SC_RESULTS     3
#define SC_DUMPING     4

/* link.mode */
#define LM_NOT        (1<<0)
#define LM_OFF        (1<<1)
#define LM_STATIC     (1<<2)
#define LM_LOCAL      (1<<3)

struct link {
	int ifi;
	int seq;
	char name[NAMELEN+2];
	short flags;
	short defrt;

	uint8_t ip[4];
	uint8_t mask;

	uint8_t mode;
};

/* Persistent scan list entry. Wimon caches short-lived scan results
   for later usage. */

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
#define SF_STALE       (1<<2)

struct scan {
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

/* Wireless config automation. Singular and generally *not* bound
   to a specific device. */

/* wifi.mode */
#define WM_ROAMING     0
#define WM_FIXEDAP     1
#define WM_DISABLED    2
/* wifi.state */
#define WS_NONE        0
#define WS_TUNED       1
#define WS_CONNECTED   2
#define WS_RETRYING    3
/* wifi.flags */
#define WF_UNSAVED     (1<<0)

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

/* Primary gateway control and tracking. This is mostly to tell whether
   wimon should stop one interface before attempting to start another. */

#define UL_NONE      0
#define UL_DOWN     -1
#define UL_WIFI     -2
#define UL_EXTERNAL -3
/* and anything positive means fixed uplink ifi */

struct uplink {
	int ifi;
	int cnt;
	uint8_t gw[4];
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
extern int sigchld;

/* wimon.c */

void schedule(void (*call)(void), int secs);

/* wimon_rtnl.c and wimon_genl.c */

void setup_rtnl(void);
void setup_genl(void);
void setup_ctrl(void);
void accept_ctrl(int sfd);
void unlink_ctrl(void);
void handle_rtnl(struct nlmsg* msg);
void handle_genl(struct nlmsg* msg);
void del_link_addresses(int ifi);

void trigger_scan(int ifi, int freq);
void trigger_disconnect(int ifi);
void parse_scan_result(struct nlgen* msg);

/* wimon_slot.c */

struct link* find_link_slot(int ifi);
struct link* grab_link_slot(int ifi);
void free_link_slot(struct link* ls);

struct scan* grab_scan_slot(uint8_t* bssid);
void drop_scan_slots(void);
void free_scan_slot(struct scan* sc);

struct child* grab_child_slot(void);
struct child* find_child_slot(int pid);
void free_child_slot(struct child* ch);

/* wimon_link.c */

void link_new(struct link* ls);
void link_gone(struct link* ls);
void link_down(struct link* ls);
void link_nl80211(struct link* ls);
void link_enabled(struct link* ls);
void link_carrier(struct link* ls);
void link_ipaddr(struct link* ls);
void link_ipgone(struct link* ls);
void link_apgone(struct link* ls);
void link_child_exit(struct link* ls, int status);

int any_pids_left(void);
void terminate_link(struct link* ls);
void finalize_links(void);
int stop_all_links(void);
int switch_uplink(int ifi);

/* wimon_wifi.c */

void wifi_ready(struct link* ls);
void wifi_gone(struct link* ls);
void wifi_connected(struct link* ls);
void wifi_conn_fail(struct link* ls);

void wifi_scan_done(void);
void wifi_scan_fail(int err);

int grab_wifi_device(int rifi);

void wifi_mode_disabled(void);
int wifi_mode_roaming(void);
int wifi_mode_fixedap(uint8_t* ssid, int slen);

/* wimon_proc.c */

void spawn_dhcp(struct link* ls, char* opts);
void spawn_wpa(struct link* ls, char* mode);
void stop_link_procs(struct link* ls, int drop);
void stop_all_procs(void);
void waitpids(void);

/* wimon_save.c */

int load_link(struct link* ls);
void save_link(struct link* ls);

int saved_psk_prio(uint8_t* ssid, int slen);

int load_psk(uint8_t* ssid, int slen, char* psk, int plen);
void save_psk(uint8_t* ssid, int slen, char* psk, int plen);

/* wimon_ctrl.c */

/* latch.ifi */
#define NONE 0
#define WIFI -1
/* latch.evt */
#define CONF 1
#define DOWN 2
#define SCAN 3

void unlatch(int ifi, int evt, int err);
