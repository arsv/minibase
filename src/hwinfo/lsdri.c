#include <bits/ioctl/drm.h>
#include <bits/ioctl/drm/mode.h>

#include <sys/file.h>
#include <sys/dents.h>
#include <sys/ioctl.h>

#include <errtag.h>
#include <output.h>
#include <string.h>
#include <format.h>
#include <util.h>

ERRTAG("lsdri");

#define OPTS "a"
#define OPT_a (1<<0)

struct top {
	int fd;
	int all;
	char* path;
	struct bufout bo;
};

static char outbuf[4096];

#define CTX struct top* ctx

#define SET_STRING(res, ptr, count) \
	res.ptr = alloca(res.count)

#define SET_ARRAY(res, ptr, count, type) \
	res.ptr = (uint64_t)alloca(res.count * sizeof(type))

#define GET_ARRAY(res, ptr, type, i) \
	(((type*)(res.ptr))[i])

#define IOCTL(cmd, arg) ioctl(ctx, cmd, arg, #cmd)

static const char* const conntypes[] = {
	[0] = "Unknown",
	[1] = "VGA",
	[2] = "DVII",
	[3] = "DVID",
	[4] = "DVIA",
	[5] = "Composite",
	[6] = "SVIDEO",
	[7] = "LVDS",
	[8] = "Component",
	[9] = "9PinDIN",
	[10] = "DisplayPort",
	[11] = "HDMIA",
	[12] = "HDMIB",
	[13] = "TV",
	[14] = "eDP",
	[15] = "VIRTUAL",
	[16] = "DSI",
	[17] = "DPI"
};

static const char* const enctypes[] = {
	[0] = "NONE",
	[1] = "DAC",
	[2] = "TMDS",
	[3] = "LVDS",
	[4] = "TVDAC",
	[5] = "VIRTUAL",
	[6] = "DSI",
	[7] = "DPMST",
	[8] = "DPI",
};

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

static void outstr(CTX, char* str)
{
	output(ctx, str, strlen(str));
}

static void outnl(CTX)
{
	output(ctx, "\n", 1);
}

static void ioctl(CTX, int req, void* out, const char* tag)
{
	int ret, fd = ctx->fd;

	if((ret = sys_ioctl(fd, req, out)) < 0)
		fail("ioctl", tag, ret);
}

static char* fmt_count(char* p, char* e, int count, const char* tag)
{
	p = fmtstr(p, e, " ");
	p = fmtint(p, e, count);
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, tag);

	if(count != 1) p = fmtstr(p, e, "s");

	return p;
}

static void dump_resources(CTX, struct drm_mode_card_res* res,
                                struct drm_mode_get_plane_res* plr)
{
	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, " ");
	p = fmt_count(p, e, res->count_crtcs,      "crtc");
	p = fmt_count(p, e, plr->count_planes,     "plane");
	p = fmt_count(p, e, res->count_encoders,   "encoder");
	p = fmt_count(p, e, res->count_connectors, "connector");

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static void dump_crtc(CTX, struct drm_mode_crtc* crt)
{
	if(!ctx->all && !crt->mode_valid)
		return;

	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, "  crtc #");
	p = fmtint(p, e, crt->crtc_id);

	if(crt->mode_valid) {
		struct drm_mode_modeinfo* mi = &crt->mode;

		p = fmtstr(p, e, " ");
		p = fmtint(p, e, mi->hdisplay);
		p = fmtstr(p, e, "x");
		p = fmtint(p, e, mi->vdisplay);
		p = fmtstr(p, e, " ");
		p = fmtint(p, e, mi->vrefresh);
		p = fmtstr(p, e, "Hz");
	}

	if(crt->fb_id) {
		p = fmtstr(p, e, " <- fb #");
		p = fmtint(p, e, crt->fb_id);
	}

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static void dump_encoder(CTX, struct drm_mode_get_encoder* enc)
{
	int et = enc->encoder_type;

	if(!ctx->all && !enc->crtc_id)
		return;

	FMTBUF(p, e, buf, 50);

	p = fmtstr(p, e, "  encoder #");
	p = fmtint(p, e, enc->encoder_id);

	if(et >= 0 && et < ARRAY_SIZE(enctypes)) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, enctypes[et]);
	} else {
		p = fmtstr(p, e, " type ");
		p = fmtint(p, e, et);
	}

	if(enc->crtc_id) {
		p = fmtstr(p, e, " <- crtc #");
		p = fmtint(p, e, enc->crtc_id);
	}

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static void dump_conn(CTX, struct drm_mode_get_connector* res)
{
	int ct = res->connector_type;

	if(!ctx->all && !res->encoder_id)
		return;

	FMTBUF(p, e, buf, 50);

	p = fmtstr(p, e, "  connector #");
	p = fmtint(p, e, res->connector_id);

	if(ct >= 0 && ct < ARRAY_SIZE(conntypes)) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, conntypes[ct]);
	} else {
		p = fmtstr(p, e, " type ");
		p = fmtint(p, e, ct);
	}

	if(res->encoder_id) {
		p = fmtstr(p, e, " <- encoder #");
		p = fmtint(p, e, res->encoder_id);
	}

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static void dump_mode(CTX, struct drm_mode_modeinfo* mode)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "    mode ");
	p = fmtint(p, e, mode->hdisplay);
	p = fmtstr(p, e, "x");
	p = fmtint(p, e, mode->vdisplay);
	p = fmtstr(p, e, " ");
	p = fmtint(p, e, mode->vrefresh);
	p = fmtstr(p, e, "Hz");
	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static void query_crtc(CTX, int id)
{
	struct drm_mode_crtc crt;

	memzero(&crt, sizeof(crt));

	crt.crtc_id = id;

	IOCTL(DRM_IOCTL_MODE_GETCRTC, &crt);

	dump_crtc(ctx, &crt);
}

static void query_encoder(CTX, int id)
{
	struct drm_mode_get_encoder enc;

	memzero(&enc, sizeof(enc));

	enc.encoder_id = id;

	IOCTL(DRM_IOCTL_MODE_GETENCODER, &enc);

	dump_encoder(ctx, &enc);
}

static void query_connector(CTX, int ci)
{
	struct drm_mode_get_connector res;

	memzero(&res, sizeof(res));
	res.connector_id = ci;

	IOCTL(DRM_IOCTL_MODE_GETCONNECTOR, &res);

	SET_ARRAY(res, props_ptr, count_props, uint32_t);
	SET_ARRAY(res, prop_values_ptr, count_props, uint64_t);
	SET_ARRAY(res, modes_ptr, count_modes, struct drm_mode_modeinfo);
	SET_ARRAY(res, encoders_ptr, count_encoders, uint32_t);

	IOCTL(DRM_IOCTL_MODE_GETCONNECTOR, &res);

	dump_conn(ctx, &res);

	for(int i = 0; i < res.count_modes; i++) {
		struct drm_mode_modeinfo* modes = (void*)res.modes_ptr;
		dump_mode(ctx, &modes[i]);
	}
}

static void query_resources(CTX)
{
	struct drm_mode_card_res res;
	struct drm_mode_get_plane_res plr;

	memzero(&res, sizeof(res));
	memzero(&plr, sizeof(plr));

	IOCTL(DRM_IOCTL_MODE_GETRESOURCES, &res);
	SET_ARRAY(res, fb_id_ptr, count_fbs, uint32_t);
	SET_ARRAY(res, crtc_id_ptr, count_crtcs, uint32_t);
	SET_ARRAY(res, connector_id_ptr, count_connectors, uint32_t);
	SET_ARRAY(res, encoder_id_ptr, count_encoders, uint32_t);
	IOCTL(DRM_IOCTL_MODE_GETRESOURCES, &res);

	IOCTL(DRM_IOCTL_MODE_GETPLANERESOURCES, &plr);

	dump_resources(ctx, &res, &plr);

	for(int i = 0; i < res.count_crtcs; i++) {
		int ci = GET_ARRAY(res, crtc_id_ptr, uint32_t, i);
		query_crtc(ctx, ci);
	}

	for(int i = 0; i < res.count_encoders; i++) {
		int ei = GET_ARRAY(res, encoder_id_ptr, uint32_t, i);
		query_encoder(ctx, ei);
	}

	for(int i = 0; i < res.count_connectors; i++) {
		int ci = GET_ARRAY(res, connector_id_ptr, uint32_t, i);
		query_connector(ctx, ci);
	}
}

static void query_info(CTX)
{
	struct drm_version version;
	struct drm_unique unique;

	memzero(&version, sizeof(version));
	memzero(&unique, sizeof(unique));

	IOCTL(DRM_IOCTL_VERSION, &version);
	SET_STRING(version, name, name_len);
	SET_STRING(version, date, date_len);
	SET_STRING(version, desc, desc_len);
	IOCTL(DRM_IOCTL_VERSION, &version);

	IOCTL(DRM_IOCTL_GET_UNIQUE, &unique);
	SET_STRING(unique, unique, unique_len);
	IOCTL(DRM_IOCTL_GET_UNIQUE, &unique);

	outstr(ctx, ctx->path);
	outstr(ctx, ":\n");
	outstr(ctx, "  ");
	output(ctx, version.name, version.name_len);
	outstr(ctx, " ");
	output(ctx, unique.unique, unique.unique_len);
	outstr(ctx, " ");
	output(ctx, version.desc, version.desc_len);
	outstr(ctx, " (");
	output(ctx, version.date, version.date_len);
	outstr(ctx, ")\n");
}

static void report_device(CTX, int di)
{
	int fd;

	FMTBUF(p, e, path, 50);
	p = fmtstr(p, e, "/dev/dri/card");
	p = fmtint(p, e, di);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		return warn(NULL, path, fd);

	ctx->fd = fd;
	ctx->path = path;

	query_info(ctx);
	query_resources(ctx);

	sys_close(ctx->fd);

	ctx->fd = -1;
	ctx->path = NULL;
}

static void scan_devices(uint* mask)
{
	char* dir = "/dev/dri";
	char buf[1024];
	int fd, rd, i;
	char* p;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail("no DRI devices found", NULL, 0);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;
			ptr += de->reclen;

			if(!de->reclen)
				continue;
			if(strncmp(de->name, "card", 4))
				continue;

			p = de->name + 4;

			if(!(p = parseint(p, &i)) || *p)
				continue;

			if(i >= 8*sizeof(mask)) {
				warn("ignoring", de->name, 0);
				continue;
			}

			*mask |= (1<<i);
		}
	}

	sys_close(fd);

}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	struct bufout* bo = &ctx->bo;
	uint mask = 0, prev = 0;
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	ctx->all = (opts & OPT_a);

	bo->fd = STDOUT;
	bo->buf = outbuf;
	bo->ptr = 0;
	bo->len = sizeof(outbuf);

	scan_devices(&mask);

	for(i = 0; i < 8*sizeof(mask); i++) {
		if(!(mask & (1<<i)))
			continue;
		if(prev)
			outnl(ctx);

		report_device(ctx, i);

		prev = 1;
	}

	bufoutflush(bo);

	return 0;
}
