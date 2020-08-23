#include <bits/types.h>

struct nlgen;
struct ncbuf;
struct nrbuf;

#define MSG struct nlgen* msg __unused

extern uint nlseq;
extern uint scanseq;
extern uint authseq;
extern int nl80211; /* family id */
extern int drvconnect;

extern struct ncbuf nc;
extern struct nrbuf nr;

void reset_auth_state(void);
void reset_scan_state(void);

void nlm_scan_done(void);
void nlm_scan_error(int err);
void nlm_auth_error(int err);

void nlm_authenticate(MSG);
void nlm_associate(MSG);
void nlm_connect(MSG);
void nlm_disconnect(MSG);

void nlm_trigger_scan(MSG);
void nlm_scan_results(MSG);
void nlm_scan_aborted(MSG);

int init_netlink(int fd, int ifi);
