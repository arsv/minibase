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
	int keyidx;
	char fs[10];
	char name[30];
};

extern struct bdev bdevs[NBDEVS];
extern struct part parts[NPARTS];

extern int nbdevs;
extern int nparts;

void load_config(void);
void link_parts(void);

void open_udev(void);
void scan_devs(void);
void wait_udev(void);

int match_dev(char* name);
void match_part(char* name);
