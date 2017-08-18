#define SCRYPT_N (1<<16)
#define SCRYPT_R 8
#define SCRYPT_P 1

#ifdef DEVEL
#define MAPDIR "./dev/mapper"
#define BLKTAB "./etc/blktab"
#define KEYFILE "./etc/dekeys"
#else
#define MAPDIR "/dev/mapper"
#define BLKTAB "/etc/blktab"
#define KEYFILE "/etc/dekeys"
#endif
