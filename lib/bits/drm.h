#include <bits/ints.h>

#define DRM_EVENT_VBLANK 0x01
#define DRM_EVENT_FLIP_COMPLETE 0x02

struct drm_event {
	uint32_t type;
	uint32_t length;
};

struct drm_event_vblank {
	uint32_t type;
	uint32_t length;

	uint64_t data;
	uint32_t sec;
	uint32_t usec;
	uint32_t seq;
	uint32_t _;
};
