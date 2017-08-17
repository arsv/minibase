#include <bits/ints.h>

#define MAX_CONFIG_SIZE 4092

#define NBDEVS 10
#define NPARTS 20

#define BY_NAME 0
#define BY_PG80 1
#define BY_CID  2

struct bdev {
	int type;
	int here;
	char id[50];
	char name[30];
};

struct part {
	int devidx;
	int here;
	char part[10];
	char label[20];
	char fs[10];
	char name[30];
	uint64_t rdev;
	uint64_t size;
	int keyidx;
};

extern struct keyfile keyfile;

extern struct bdev bdevs[NBDEVS];
extern struct part parts[NPARTS];

extern int nbdevs;
extern int nparts;

void load_config(void);

void open_udev(void);
void scan_devs(void);
void wait_udev(void);
int any_missing_devs(void);

int match_dev(char* name);
void match_part(char* name);


void setup_devices(void);
int check_partitions(void);

int error(const char* msg, char* arg, int err);
void* key_by_idx(int idx);

void status(char* msg);
void message(char* msg, int ms);
int input(char* title, char* buf, int len);

void link_plain_partitions(void);

void term_init(void);
void term_fini(void);
void quit(const char* msg, char* arg, int err);
void open_dm_control(void);

int try_passphrase(char* phrase, int len);
int check_keyindex(int ki);
void* get_key(int ki);
void term_back(void);
void clearbox(void);
void query_part_inodes(void);
