#include <cdefs.h>

#define SSIDLEN 32
#define NCONNS 10
#define NSCANS 30

#define MACLEN 6

/* authstate */
#define AS_IDLE            0
#define AS_AUTHENTICATING  1
#define AS_ASSOCIATING     2
#define AS_CONNECTING      3
#define AS_CONNECTED       4
#define AS_DISCONNECTING   5
#define AS_NETDOWN         7

/* eapolstate */
#define ES_IDLE            0
#define ES_WAITING_1_4     1
#define ES_WAITING_2_4     2
#define ES_WAITING_3_4     3
#define ES_NEGOTIATED      4

/* scanstate */
#define SS_IDLE            0
#define SS_SCANNING        1
#define SS_SCANDUMP        2

/* opermode */
#define OP_STOPPED         0 /* no scanning, just keep the device */
#define OP_DETACH          1 /* drop currently active device */
#define OP_MONITOR         2 /* passive scanning */
#define OP_ONESHOT         3 /* connecting to AP, auto-reset on failure */
#define OP_ACTIVE          4 /* AP set, at least one successful connection */
#define OP_RESCAN          5 /* lost connection, re-trying the same BSS */

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
#define SF_TRIED       (1<<1)
#define SF_STALE       (1<<2)
#define SF_GOOD        (1<<3)
#define SF_TKIP        (1<<4)

struct scan {
	int flags;
	ushort freq;
	short signal;
	byte bssid[6];
	ushort ieslen;
	byte* ies;
};

struct conn {
	int fd;
	int rep;
};

extern char** environ;

extern char ifname[32];
extern int ifindex;
extern byte ifaddr[6];

extern int ctrlfd;    /* control socket */
extern int rfkill;    /* fd, /dev/rfkill */
extern int rawsock;   /* fd, EAPOL socket */
extern int netlink;   /* fd, GENL */

extern struct scan scans[];
extern struct conn conns[];
extern int nscans;
extern int nconns;

extern int opermode;
extern int scanstate;
extern int authstate;
extern int rfkilled;

extern struct heap {
	void* org;
	void* ptr;
	void* brk;
} hp;

/* The AP we're tuned on */

extern struct ap {
	byte bssid[6];
	short freq;
	short signal;
	ushort slen;
	byte ssid[SSIDLEN];
	const void* txies;
	uint iesize;

	int tkipgroup;

	int success;
	int rescans;
} ap;

/* Encryption parameters */

extern byte PSK[32];
extern byte amac[6]; /* == ap.bssid */
extern byte smac[6];
/* see definitions for these */
extern byte KCK[16];
extern byte KEK[16];
extern byte PTK[16];
extern byte GTK[32];
extern byte RSC[6];
extern int gtkindex;
extern int pollset;

void setup_netlink(void);
void setup_iface(char* name);
void setup_control(void);
void unlink_control(void);
int open_rawsock(void);
void close_rawsock(void);

int open_netlink();
void close_netlink();
void handle_netlink(void);
void handle_rawsock(void);
void handle_control(void);
void handle_conn(struct conn* cn);
void handle_rfkill(void);
void retry_rfkill(void);
void close_rfkill(void);

void upload_ptk(void);
void upload_gtk(void);
void prime_eapol_state(void);
void allow_eapol_sends(void);
void reset_eapol_state(void);
int start_full_scan(void);
int start_void_scan(void);
int start_scan(int freq);
int start_disconnect(void);
int start_connection(void);
int force_disconnect(void);

void quit(const char* msg, char* arg, int err) noreturn;
void abort_connection(void);

struct scan* grab_scan_slot(byte bssid[6]);
struct conn* grab_conn_slot(void);
void free_scan_slot(struct scan* sc);

void parse_station_ies(struct scan* sc, char* buf, uint len);
struct scan* find_scan_slot(byte bssid[6]);

void reconnect_to_current_ap(void);
void reassess_wifi_situation(void);
void handle_connect(void);
void handle_disconnect(void);
void handle_rfrestored(void);
void handle_external(void);
void handle_timeout(void);
void check_new_scan_results(void);
int run_stamped_scan(void);

int got_psk_for(byte* ssid, int slen);
int load_psk(byte* ssid, int slen, byte psk[32]);
void save_psk(byte* ssid, int slen, byte psk[32]);
int drop_psk(byte* ssid, int slen);

void set_timer(int seconds);
void clr_timer(void);
int get_timer(void);

void reset_station(void);
void clear_device(void);
int set_station(byte* ssid, int slen, byte psk[32]);

void report_net_down(void);
void report_scanning(void);
void report_scan_done(void);
void report_scan_fail(void);
void report_no_connect(void);
void report_disconnect(void);
void report_connected(void);
void report_external(void);
void report_aborted(void);

void trigger_dhcp(void);

void routine_fg_scan(void);
void routine_bg_scan(void);
int maybe_start_scan(void);
void note_disconnect(void);
void note_nl_timeout(void);

void reset_device(void);
void handle_netdown(void);
int set_device(char* name);
int bring_iface_up(void);
void clear_scan_table(void);

void init_heap_ptrs(void);
int extend_heap(uint size);
void* heap_store(void* buf, int len);
void maybe_trim_heap(void);
