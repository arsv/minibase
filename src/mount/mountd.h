#define NCONNS 10

#define FS_EXT4    1
#define FS_ISO9660 2

struct ucmsg;
struct ucbuf;

void quit(const char* msg, char* arg, int err) __attribute__((noreturn));
void handle(int fd);

int reply(int fd, int cmd, int attr, char* value);

int check_blkdev(char* name, char* path, int isloop);
int prep_fs_options(char* buf, int len, int fstype, struct ucbuf* uc);
const char* fs_type_string(int fst);

int setup_loopback(int fd, char* name);
int unset_loopback(int idx);
