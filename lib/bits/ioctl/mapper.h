#include <bits/types.h>
#include <bits/ioctl.h>

#define DM_VERSION_MAJOR      4
#define DM_VERSION_MINOR     36
#define DM_VERSION_PATCHLEVEL 0

#define DM_VERSION           _IOWR(0xFD,  0, struct dm_ioctl)
#define DM_REMOVE_ALL        _IOWR(0xFD,  1, struct dm_ioctl)
#define DM_LIST_DEVICES      _IOWR(0xFD,  2, struct dm_ioctl)

#define DM_DEV_CREATE        _IOWR(0xFD,  3, struct dm_ioctl)
#define DM_DEV_REMOVE        _IOWR(0xFD,  4, struct dm_ioctl)
#define DM_DEV_RENAME        _IOWR(0xFD,  5, struct dm_ioctl)
#define DM_DEV_SUSPEND       _IOWR(0xFD,  6, struct dm_ioctl)
#define DM_DEV_STATUS        _IOWR(0xFD,  7, struct dm_ioctl)
#define DM_DEV_WAIT          _IOWR(0xFD,  8, struct dm_ioctl)
#define DM_DEV_ARM_POLL      _IOWR(0xFD, 16, struct dm_ioctl)

#define DM_TABLE_LOAD        _IOWR(0xFD,  9, struct dm_ioctl)
#define DM_TABLE_CLEAR       _IOWR(0xFD, 10, struct dm_ioctl)
#define DM_TABLE_DEPS        _IOWR(0xFD, 11, struct dm_ioctl)
#define DM_TABLE_STATUS      _IOWR(0xFD, 12, struct dm_ioctl)

#define DM_LIST_VERSIONS     _IOWR(0xFD, 13, struct dm_ioctl)

#define DM_TARGET_MSG        _IOWR(0xFD, 14, struct dm_ioctl)
#define DM_DEV_SET_GEOMETRY  _IOWR(0xFD, 15, struct dm_ioctl)

/* dm_ioctl.flags */
#define DM_READONLY_FLAG              (1<<0)
#define DM_SUSPEND_FLAG               (1<<1)
#define DM_EXISTS_FLAG                (1<<2)
#define DM_PERSISTENT_DEV_FLAG        (1<<3)
#define DM_STATUS_TABLE_FLAG          (1<<4)
#define DM_ACTIVE_PRESENT_FLAG        (1<<5)
#define DM_INACTIVE_PRESENT_FLAG      (1<<6)
#define DM_BUFFER_FULL_FLAG           (1<<8)
#define DM_SKIP_BDGET_FLAG            (1<<9)
#define DM_SKIP_LOCKFS_FLAG           (1<<10)
#define DM_NOFLUSH_FLAG               (1<<11)
#define DM_QUERY_INACTIVE_TABLE_FLAG  (1<<12)
#define DM_UEVENT_GENERATED_FLAG      (1<<13)
#define DM_UUID_FLAG                  (1<<14)
#define DM_SECURE_DATA_FLAG           (1<<15)
#define DM_DATA_OUT_FLAG              (1<<16)
#define DM_DEFERRED_REMOVE            (1<<17)
#define DM_INTERNAL_SUSPEND_FLAG      (1<<18)

struct dm_ioctl {
	uint32_t version[3];
	uint32_t data_size;
	uint32_t data_start;
	uint32_t target_count;
	int32_t open_count;
	uint32_t flags;
	uint32_t event_nr;
	uint32_t padding;
	uint64_t dev;

	char name[128];
	char uuid[129];

	char data[7];
};

struct dm_target_spec {
	uint64_t start;
	uint64_t length;
	int32_t status;
	uint32_t next;
	char type[16];
	char params[];
};

struct dm_target_deps {
	uint32_t count;
	uint32_t padding;
	uint64_t dev[];
};

struct dm_name_list {
	uint64_t dev;
	uint32_t next;
	char name[];
};

struct dm_target_versions {
        uint32_t next;
        uint32_t version[3];
        char name[];
};

struct dm_target_msg {
	uint64_t sector;
	char message[];
};
