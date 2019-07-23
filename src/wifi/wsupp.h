#include <cdefs.h>

#define SSIDLEN 32
#define NCONNS 10
#define NSCANS 30

#define MACLEN 6

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

extern int operstate;
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
	/* network name */
	ushort slen;
	byte ssid[SSIDLEN];
	/* encryption settings */
	const void* txies;
	uint iesize;
	uint gtklen;
	uint akm;
	uint pairwise;
	uint group;
	/* current BSS data */
	byte bssid[6];
	short freq;
	short signal;
	int success;
	int rescans;
} ap;

/* Netlink connection */

extern struct netlink nl;
extern int netlink; /* socket fd (also nl.fd) */
extern int nl80211; /* family, for messages */

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

void quit(const char* msg, char* arg, int err) noreturn;

/* Timer */
typedef void (*callptr)(void);
void set_timer(int seconds, callptr cb);
int get_timer(void);
void clear_timer(void);

/* Control */
void setup_control(void);
void unlink_control(void);
void handle_control(void);
void handle_conn(struct conn* cn);

void report_scan_end(int err);
void report_connecting(void);
void report_disconnect(void);
void report_no_connect(void);
void report_link_ready(void);

/* RFkill section */
void handle_rfkill(void);
void retry_rfkill(void);
void close_rfkill(void);
void radio_restored(void);
void radio_killed(void);

/* EAPOL section */
int open_rawsock(void);
void close_rawsock(void);
void handle_rawsock(void);
int prime_eapol_state(void);
int allow_eapol_sends(void);
void reset_eapol_state(void);
void eapol_failure(void);
void eapol_success(void);

/* Netlink section */
int open_netlink(int ifi);
void close_netlink(void);
void handle_netlink(void);
void reset_auth_state(void);
void reset_scan_state(void);
int start_scan(int freq);
void scan_ended(int err);
int start_disconnect(void);
int start_connection(void);
int force_disconnect(void);
void abort_connection(int err);
void connection_ready(void);
void connection_ended(int err);
int upload_ptk(void);
int upload_gtk(void);

/* Slots and memory section */
struct scan* grab_scan_slot(byte bssid[6]);
struct conn* grab_conn_slot(void);
void free_scan_slot(struct scan* sc);
void clear_scan_table(void);

void init_heap_ptrs(void);
int extend_heap(uint size);
void* heap_store(void* buf, int len);
void maybe_trim_heap(void);

/* BSS utilities */
int pick_best_bss(void);
int current_bss_in_scans(void);
void mark_current_bss_good(void);
void clear_all_bss_marks(void);

/* Top-level state machine */
int ap_monitor(void);
int ap_connect(byte* ssid, int slen, byte psk[32]);
int ap_disconnect(void);
int ap_detach(void);
int ap_reset(void);
int time_to_scan(void);

void trigger_dhcp(void);
