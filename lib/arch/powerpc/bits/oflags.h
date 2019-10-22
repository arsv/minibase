#define O_RDONLY        (0<<0)
#define O_WRONLY        (1<<0)
#define O_RDWR          (1<<1)

#define O_APPEND        02000
#define O_DSYNC         010000
#define O_NONBLOCK      04000
#define O_CREAT         0100
#define O_TRUNC         01000
#define O_EXCL          0200
#define O_NOCTTY        0400
#define O_LARGEFILE     0200000
#define O_DIRECT        0400000

#define O_DIRECTORY     040000
#define O_NOFOLLOW      0100000
#define O_NOATIME       01000000
#define O_CLOEXEC       02000000

#define O_PATH          010000000
#define O_TMPFILE       020040000

#define O_DSYNC      010000
#define O_SYNC     04010000
#define O_RSYNC    04010000

