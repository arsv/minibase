#define NCONNS 10

#define FS_EXT4    1
#define FS_VFAT    2
#define FS_ISO9660 3

struct ucmsg;
struct ucbuf;
struct ucred;

void quit(const char* msg, char* arg, int err) __attribute__((noreturn));
void handle(int fd);

int reply(int fd, int cmd, int attr, char* value);

int check_blkdev(char* name, char* path, int isloop);
int prep_fs_options(char* buf, int len, int fstype, struct ucred* uc);
const char* fs_type_string(int fst);

int check_if_loop_mount(char* mntpoint);
int setup_loopback(int fd, char* name);
int unset_loopback(int idx);
