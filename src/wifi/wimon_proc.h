#include <bits/ints.h>

struct wpaconf {
	char freq[10];
	char bssid[6*3];
	char ssid[34];
	char mode[4];
	char psk[70];
};

struct link;

void link_new(struct link* ls);
void link_del(struct link* ls);
void link_wifi(struct link* ls);
void link_enabled(struct link* ls);
void link_carrier(struct link* ls);
void link_down(struct link* ls);
void link_scan(struct link* ls);
void link_addr(struct link* ls);
