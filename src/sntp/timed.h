#include <bits/time.h>
#include <cdefs.h>

#define NCONNS 4
#define NSERVS 4
#define NPOLLS (2 + NCONNS)
#define NPOINT 16

/* top.state */
#define TS_IDLE        0
#define TS_SELECT      1
#define TS_PING_SENT   2
#define TS_PING_WAIT   3
#define TS_POLL_SENT   4
#define TS_POLL_WAIT   5

/* top.tistate */
#define TI_NONE        0
#define TI_CLEAR       1
#define TI_ARMED       2

#define SF_SET  (1<<0)
#define SF_IPv6 (1<<1)
#define SF_KILL (1<<2)
#define SF_FAIL (1<<3)
#define SF_PING (1<<4)
#define SF_RTT  (1<<5)

#define ADDRLEN 32

#define MIN_POLL_INTERVAL (1<<10)
#define MAX_POLL_INTERVAL (1<<12)

struct pollfd;
struct ucmsg;

struct ntpreq {
	uint32_t code;
	uint32_t rdelay;
	uint32_t rdispersion;
	byte refid[4];
	uint64_t reference;
	uint64_t originate;
	uint64_t receive;
	uint64_t transmit;
} __attribute__((packed));

struct conn {
	int fd;
};

struct serv {
	ushort flags;
	ushort port;
	byte addr[16];
	uint rtt;
};

struct top {
	int ntpfd;
	int ctlfd;

	int nconn;
	struct conn conns[NCONNS];
	struct serv servs[NSERVS];

	int pollready;
	struct timespec alarm;
	uint interval;
	uint pollexp;

	int timerid;
	int tistate;

	int state;

	uint64_t sendtime;
	uint64_t reference;

	int current; /* index of one of the servs[] above, or < 0 if none */
	int failures;

	int bestidx;
	uint bestrtt;
	uint lastrtt;

	uint64_t ref;
	int64_t lo;
	int64_t hi;

	uint64_t polltime;
	uint64_t synctime;
	int64_t syncdt;
};

#define CTX struct top* ctx __attribute__((unused))
#define CN struct conn* cn __attribute__((unused))
#define MSG struct ucmsg* msg __attribute__((unused))

void quit(const char* msg, char* arg, int err) noreturn;

void init_clock_state(CTX);

void check_client(CTX, CN);
void check_packet(CTX);
void handle_timeout(CTX);

void set_timed(CTX, int state, int sec);
void stop_service(CTX);

struct serv* current(CTX);

void consider_synching(CTX);
