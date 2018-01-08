#include <bits/types.h>
#include <bits/ioctl.h>

struct drm_version {
	int major;
	int minor;
	int patchlevel;
	ulong name_len;
	char *name;
	ulong date_len;
	char *date;
	ulong desc_len;
	char *desc;
};

struct drm_unique {
	ulong unique_len;
	char *unique;
};

#define DRM_IOCTL_VERSION                _IOWR('d', 0x00, struct drm_version)
#define DRM_IOCTL_GET_UNIQUE             _IOWR('d', 0x01, struct drm_unique)
#define DRM_IOCTL_GET_STATS              _IOR( 'd', 0x06, struct drm_stats)
#define DRM_IOCTL_SET_MASTER             _IO(  'd', 0x1e)
#define DRM_IOCTL_DROP_MASTER            _IO(  'd', 0x1f)

#define DRM_IOCTL_MODE_GETRESOURCES      _IOWR('d', 0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC           _IOWR('d', 0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER        _IOWR('d', 0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR      _IOWR('d', 0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETPLANERESOURCES _IOWR('d', 0xB5, struct drm_mode_get_plane_res)
