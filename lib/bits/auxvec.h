#define AT_UID       11
#define AT_EUID      12
#define AT_GID       13
#define AT_EGID      14
#define AT_RANDOM    25

struct auxvec {
	long key;
	long val;
};
