#include <bits/ints.h>

extern int ifindex;
extern char* ifname;
extern int frequency;
extern uint8_t bssid[6];
extern char* ssid;

extern int netlink;
extern int rawsock;

extern uint8_t PSK[32];
extern uint8_t PTK[16];
extern uint8_t GTK[32];
extern uint8_t RSC[6];
extern int gtkindex;
extern int tkipgroup;
extern const char* ies;
extern int iesize;

void setup_netlink(void);
int resolve_ifname(char* name);
void authenticate(void);
void associate(void);
void upload_ptk(void);
void upload_gtk(void);
void pull_netlink(void);
void terminate(void);

void open_rawsock(void);
void negotiate_keys(void);
int group_rekey(void);
void cleanup_keys(void);

void quit(const char* msg, const char* arg, int err) noreturn;
