#include <bits/ints.h>

void release_config(void);

int load_link(struct link* ls);
void save_link(struct link* ls);

int load_psk(uint8_t* ssid, int slen, char* psk, int plen);
void save_psk(uint8_t* ssid, int slen, char* psk, int plen);
