#include <bits/ints.h>

#define LO_NAME_SIZE  64
#define LO_KEY_SIZE   32

#define LO_FLAGS_READ_ONLY     (1<<0)
#define LO_FLAGS_AUTOCLEAR     (1<<2)
#define LO_FLAGS_PARTSCAN      (1<<3)
#define LO_FLAGS_DIRECT_IO     (1<<4)
#define LO_FLAGS_BLOCKSIZE     (1<<5)

struct loop_info64 {
	uint64_t device;
	uint64_t inode;
	uint64_t rdevice;
	uint64_t offset;
	uint64_t sizelimit;
	uint32_t number;
	uint32_t encrypt_type;
	uint32_t encrypt_key_size;
	uint32_t flags;
	uint8_t file_name[LO_NAME_SIZE];
	uint8_t crypt_name[LO_NAME_SIZE];
	uint8_t encrypt_key[LO_KEY_SIZE];
	uint64_t init[2];
};

#define LOOP_SET_FD            0x4C00
#define LOOP_CLR_FD            0x4C01
#define LOOP_SET_STATUS64      0x4C04
#define LOOP_GET_STATUS64      0x4C05
#define LOOP_CHANGE_FD         0x4C06
#define LOOP_SET_CAPACITY      0x4C07
#define LOOP_SET_DIRECT_IO     0x4C08

#define LOOP_CTL_ADD           0x4C80
#define LOOP_CTL_REMOVE        0x4C81
#define LOOP_CTL_GET_FREE      0x4C82
