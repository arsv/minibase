#include <bits/ints.h>

extern int ifindex;
extern char* ifname;
extern int frequency;
extern uint8_t bssid[6];
extern char* ssid;

extern uint8_t PSK[32];
extern uint8_t PTK[16];
extern uint8_t GTK[32];

void setup_netlink(void);
void reset_netlink(void);
int resolve_ifname(char* name);
void authenticate(void);
void associate(void);
void upload_keys(void);

void open_rawsock(void);
void negotiate_keys(void);
void cleanup_keys(void);
