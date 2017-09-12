#include "findblk.h"

void setup_devices(void);
void unset_devices(void);
int check_partitions(void);

void* key_by_idx(int idx);

void status(char* msg);
void message(char* msg, int ms);
int input(char* title, char* buf, int len);

void link_plain_partitions(void);

void term_init(void);
void term_fini(void);
void open_dm_control(void);

int try_passphrase(char* phrase, int len);
int check_keyindex(int ki);
void* get_key(int ki);
void wipe_keyfile(void);
void term_back(void);
void clearbox(void);
void query_part_inodes(void);
void load_keyfile(void);
