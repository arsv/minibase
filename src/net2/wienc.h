#include <cdefs.h>

#define SSIDLEN 32
#define NCONNS 10
#define NSCANS 20

#define MACLEN 6

#define AS_IDLE            0
#define AS_AUTHENTICATING  1
#define AS_ASSOCIATING     2
#define AS_CONNECTING      3
#define AS_CONNECTED       4
#define AS_DISCONNECTING   5
#define AS_NETDOWN         7

#define ES_IDLE            0
#define ES_WAITING_1_4     1
#define ES_WAITING_2_4     2
#define ES_WAITING_3_4     3
#define ES_NEGOTIATED      4

#define SS_IDLE            0
#define SS_SCANNING        1
#define SS_SCANDUMP        2

#define OP_EXIT            0
#define OP_EXITREQ         1
#define OP_NEUTRAL         2
#define OP_ONESHOT         3
#define OP_ENABLED         4
#define OP_RESCAN          5

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
#define SF_PASS        (1<<2)
#define SF_STALE       (1<<3)
#define SF_TRIED       (1<<4)

struct scan {
	short freq;
	short signal;
	short flags;
	short type;
	uint8_t bssid[6];
	ushort slen;
	uint8_t ssid[SSIDLEN];
};

struct conn {
	int fd;
	int rep;
};

extern char* ifname;
extern int ifindex;
extern int ctrlfd;
extern int rfkill;
extern int rawsock;
extern int netlink;

extern struct scan scans[];
extern struct conn conns[];
extern int nscans;
extern int nconns;

extern int opermode;
extern int scanstate;
extern int authstate;
extern int rfkilled;

/* The AP we're tuned on */

extern struct ap {
	short bssid[6];
	short freq;
	short signal;
	ushort slen;
	short type;
	uint8_t ssid[SSIDLEN];
	const void* ies;
	uint iesize;

	int fixed;
	int unsaved;
	int tkipgroup;

	int success;
} ap;

/* Config file parsing */

struct line {
	char* start;
	char* end;
};

struct chunk {
	char* start;
	char* end;
};

/* Encryption parameters */

extern byte PSK[32];
extern byte amac[6]; /* == ap.bssid */
extern byte smac[6];

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
void reopen_rawsock(void);

void handle_netlink(void);
void handle_rawsock(void);
void handle_control(void);
void handle_conn(struct conn* cn);
void handle_rfkill(void);
void retry_rfkill(void);

void upload_ptk(void);
void upload_gtk(void);
void prime_eapol_state(void);
void allow_eapol_sends(void);
void reset_eapol_state(void);
int start_scan(int freq);
int start_disconnect(void);
int start_connection(void);

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
void check_new_scan_results(void);
int run_stamped_scan(void);

int load_config(void);
void save_config(void);
void drop_config(void);

int got_psk_for(byte* ssid, int slen);
int load_psk(byte* ssid, int slen, byte psk[32]);
void save_psk(byte* ssid, int slen, byte psk[32]);

void set_timer(int seconds);
void clr_timer(void);

void reset_station(void);
int set_fixed_saved(byte* ssid, int slen);
int set_fixed_given(byte* ssid, int slen, byte psk[32]);

void report_net_down(void);
void report_scanning(void);
void report_scan_done(void);
void report_scan_fail(void);
void report_no_connect(void);
void report_disconnect(void);
void report_connected(void);
