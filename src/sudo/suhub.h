#define NPROC 50

struct ucred;

extern char** environ;

void quit(const char* msg, char* arg, int err) __attribute__((noreturn));

void reply(int fd, int rep, int attr, int val);
void handle(int fd, int* cpid);

int spawn(int* cpid, char** argv, int* fds, struct ucred* cr);
