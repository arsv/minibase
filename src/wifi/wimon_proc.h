#include <bits/ints.h>

/* conf.mode */

#define CM_NONE   0
#define CM_WPA    1
#define CM_DHCP   2
#define CM_MANUAL 3

struct conf {
	int mode;
	int ifi;

	char bssid[6];
	char ssid[NAMELEN+2];

	char key[4];
	char psk[2*32+1];
};

struct link;

extern struct conf cfg;

void link_new(struct link* ls);
void link_del(struct link* ls);
void link_wifi(struct link* ls);

void link_enabled(struct link* ls);
void link_carrier(struct link* ls);
void link_carrier_lost(struct link* ls);
void link_scan_done(struct link* ls);

void link_configured(struct link* ls);
void link_deconfed(struct link* ls);

void link_terminated(struct link* ls);

void spawn_dhcp(struct link* ls, char* opts);
void spawn_wpa(struct link* ls, struct scan* sc, char* mode, char* psk);

void terminate_link(struct link* ls);
void drop_link_procs(struct link* ls);
