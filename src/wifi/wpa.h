#include <bits/ints.h>

extern int ifindex;
extern char* ifname;
extern int frequency;
extern uint8_t bssid[6];
extern char* ssid;

extern uint8_t PSK[32];
extern uint8_t PTK[16];
extern uint8_t GTK[32];
extern uint8_t RSC[6];

void setup_netlink(void);
void open_netlink(void);
void close_netlink(void);
int resolve_ifname(char* name);
void authenticate(void);
void associate(void);
void upload_ptk(void);
void upload_gtk(void);

void open_rawsock(void);
void negotiate_keys(void);
void group_rekey(void);
void cleanup_keys(void);
