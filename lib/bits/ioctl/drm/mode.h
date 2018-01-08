struct drm_mode_card_res {
	uint64_t fb_id_ptr;
	uint64_t crtc_id_ptr;
	uint64_t connector_id_ptr;
	uint64_t encoder_id_ptr;
	uint32_t count_fbs;
	uint32_t count_crtcs;
	uint32_t count_connectors;
	uint32_t count_encoders;
	uint32_t min_width;
	uint32_t max_width;
	uint32_t min_height;
	uint32_t max_height;
};

struct drm_mode_modeinfo {
	uint32_t clock;
	uint16_t hdisplay;
	uint16_t hsync_start;
	uint16_t hsync_end;
	uint16_t htotal;
	uint16_t hskew;
	uint16_t vdisplay;
	uint16_t vsync_start;
	uint16_t vsync_end;
	uint16_t vtotal;
	uint16_t vscan;
	uint32_t vrefresh;
	uint32_t flags;
	uint32_t type;
	char name[32];
};

struct drm_mode_crtc {
	uint64_t set_connectors_ptr;
	uint32_t count_connectors;

	uint32_t crtc_id;
	uint32_t fb_id;

	uint32_t x;
	uint32_t y;

	uint32_t gamma_size;
	uint32_t mode_valid;

	struct drm_mode_modeinfo mode;
};

struct drm_mode_get_plane {
	uint32_t plane_id;

	uint32_t crtc_id;
	uint32_t fb_id;

	uint32_t possible_crtcs;
	uint32_t gamma_size;

	uint32_t count_format_types;
	uint64_t format_type_ptr;
};

struct drm_mode_get_plane_res {
	uint64_t plane_id_ptr;
	uint32_t count_planes;
};

#define DRM_MODE_ENCODER_NONE    0
#define DRM_MODE_ENCODER_DAC     1
#define DRM_MODE_ENCODER_TMDS    2
#define DRM_MODE_ENCODER_LVDS    3
#define DRM_MODE_ENCODER_TVDAC   4
#define DRM_MODE_ENCODER_VIRTUAL 5
#define DRM_MODE_ENCODER_DSI     6
#define DRM_MODE_ENCODER_DPMST   7
#define DRM_MODE_ENCODER_DPI     8

struct drm_mode_get_encoder {
	uint32_t encoder_id;
	uint32_t encoder_type;

	uint32_t crtc_id;

	uint32_t possible_crtcs;
	uint32_t possible_clones;
};

#define DRM_MODE_SUBCONNECTOR_Automatic 0
#define DRM_MODE_SUBCONNECTOR_Unknown   0
#define DRM_MODE_SUBCONNECTOR_DVID      3
#define DRM_MODE_SUBCONNECTOR_DVIA      4
#define DRM_MODE_SUBCONNECTOR_Composite 5
#define DRM_MODE_SUBCONNECTOR_SVIDEO    6
#define DRM_MODE_SUBCONNECTOR_Component 8
#define DRM_MODE_SUBCONNECTOR_SCART     9

#define DRM_MODE_CONNECTOR_Unknown      0
#define DRM_MODE_CONNECTOR_VGA          1
#define DRM_MODE_CONNECTOR_DVII         2
#define DRM_MODE_CONNECTOR_DVID         3
#define DRM_MODE_CONNECTOR_DVIA         4
#define DRM_MODE_CONNECTOR_Composite    5
#define DRM_MODE_CONNECTOR_SVIDEO       6
#define DRM_MODE_CONNECTOR_LVDS         7
#define DRM_MODE_CONNECTOR_Component    8
#define DRM_MODE_CONNECTOR_9PinDIN      9
#define DRM_MODE_CONNECTOR_DisplayPort  10
#define DRM_MODE_CONNECTOR_HDMIA        11
#define DRM_MODE_CONNECTOR_HDMIB        12
#define DRM_MODE_CONNECTOR_TV           13
#define DRM_MODE_CONNECTOR_eDP          14
#define DRM_MODE_CONNECTOR_VIRTUAL      15
#define DRM_MODE_CONNECTOR_DSI          16
#define DRM_MODE_CONNECTOR_DPI          17

struct drm_mode_get_connector {
	uint64_t encoders_ptr;
	uint64_t modes_ptr;
	uint64_t props_ptr;
	uint64_t prop_values_ptr;

	uint32_t count_modes;
	uint32_t count_props;
	uint32_t count_encoders;

	uint32_t encoder_id;
	uint32_t connector_id;
	uint32_t connector_type;
	uint32_t connector_type_id;

	uint32_t connection;
	uint32_t mm_width;
	uint32_t mm_height;
	uint32_t subpixel;

	uint32_t pad;
};
