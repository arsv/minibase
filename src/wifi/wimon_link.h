struct link;

/* wimon_link.c */

void link_new(struct link* ls);
void link_del(struct link* ls);
void link_wifi(struct link* ls);

void link_enabled(struct link* ls);
void link_carrier(struct link* ls);
void link_carrier_lost(struct link* ls);
void link_scan_done(struct link* ls);

void link_configured(struct link* ls);
void link_terminated(struct link* ls);
void link_deconfed(struct link* ls);

/* wimon_proc.c */

void spawn_dhcp(struct link* ls, char* opts);
void spawn_wpa(struct link* ls, struct scan* sc, char* mode, char* psk);

void terminate_link(struct link* ls);
void drop_link_procs(struct link* ls);
