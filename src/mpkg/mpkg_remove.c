#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fpath.h>

#include <output.h>
#include <string.h>
#include <util.h>

#include "mpkg.h"

static byte* skip_name(CTX, byte* p, byte* e)
{
	byte* z = p + PAGE;

	if(e > z) e = z;

	for(; p < e; p++)
		if(!*p) break;
	if(p >= e)
		fail("unterminated name", NULL, 0);

	return p + 1;
}

static void prep_filedb(CTX)
{
	char* name = pkg_name(ctx);
	byte* head = ctx->head;
	uint hlen = ctx->hlen;

	name = root_adjust(name);

	if(hlen < 4)
		failz(ctx, "truncated:", name, 0);

	if(memcmp(head, "MPKG", 4))
		failz(ctx, "garbage in", name, 0);

	if(hlen < 5)
		failz(ctx, "no files listed in", name, 0);

	byte lead = head[4];

	if(lead == 0xFF) {
		byte* p = head + 5;
		byte* e = head + hlen;
		byte* z = skip_name(ctx, p, e);

		setup_prefix(ctx, (char*)p);

		ctx->hoff = z - head;
		ctx->hlen = e - z;
	} else {
		ctx->hoff = 4;
		ctx->hlen = hlen - 4;
	}
}

/* Empty pass just to make sure everything goes well. */

static void validate_names(CTX)
{
	byte* p = ctx->head + ctx->hoff;
	byte* e = p + ctx->hlen;
	int depth = 0;

	while(p < e) {
		byte lead = *p++;

		p = skip_name(ctx, p, e);

		if(!(lead & TAG_DIR)) continue;

		int lvl = lead & TAG_DEPTH;

		if(lvl > depth)
			fail("invalid index entry", NULL, 0);
		if(lvl < depth)
			depth = lvl;

		depth++;
	}
}

static void unlink_dirs(CTX, int todepth)
{
	int depth = ctx->depth;
	int ret, at = ctx->at;

	while(depth > todepth) {
		depth--;

		if((ret = sys_close(at)) < 0)
			fail("close", NULL, ret);

		at = ctx->pfds[depth];

		char* name = ctx->path[depth];

		if((ret = sys_unlinkat(at, name, AT_REMOVEDIR)) >= 0)
			continue;
		if(ret == -ENOTEMPTY)
			continue;

		warnx(ctx, NULL, name, ret);
	}

	ctx->at = at;
	ctx->depth = depth;
}

static void enter_dir(CTX, char* name, byte lead)
{
	int depth = ctx->depth;
	int lvl = lead & TAG_DEPTH;

	if(lvl > depth)
		return;
	if(lvl < depth)
		unlink_dirs(ctx, depth = lvl);
	if(depth >= MAXDEPTH)
		fail("tree depth exceeded", NULL, 0);

	int fd, at = ctx->at;

	if((fd = sys_openat(at, name, O_DIRECTORY | O_PATH)) < 0) {
		if(fd == -ENOENT)
			return;
		if(fd == -ENOTDIR)
			return;
		if(fd == -ELOOP)
			return;
		warnx(ctx, NULL, name, fd);
	}

	ctx->at = fd;
	ctx->pfds[depth] = at;
	ctx->path[depth] = name;
	ctx->depth = depth + 1;
}

static void unlink_leaf(CTX, char* name)
{
	int ret, at = ctx->at;

	if((ret = sys_unlinkat(at, name, 0)) >= 0)
		return;
	if(ret == -ENOENT)
		return;

	warnx(ctx, NULL, name, ret);
}

static void unlink_files(CTX)
{
	byte* p = ctx->head + ctx->hoff;
	byte* e = p + ctx->hlen;

	while(p < e) {
		byte lead = *p++;
		char* name = (char*)p;

		p = skip_name(ctx, p, e);

		if(lead & TAG_DIR)
			enter_dir(ctx, name, lead);
		else
			unlink_leaf(ctx, name);

	}

	unlink_dirs(ctx, 0);

	if(ctx->fail) fail("unable to continue", NULL, 0);
}

void cmd_remove(CTX)
{
	int n = args_left(ctx);

	setup_root(ctx, n > 1);
	take_package_arg(ctx);
	no_more_arguments(ctx);

	load_filedb(ctx);
	prep_filedb(ctx);

	validate_names(ctx);

	unlink_files(ctx);

	unlink_filedb(ctx);
}

/* The "files" command is essentially just "remove", only that instead
   of unlinking files we print the names. */

static void push_dir(CTX, struct bufout* bo, char* name, byte lead)
{
	int depth = ctx->depth;
	int lvl = lead & TAG_DEPTH;

	if(lvl > depth)
		return;
	if(lvl < depth)
		depth = lvl;

	if(depth >= MAXDEPTH) {
		bufoutflush(bo);
		fail("tree depth exceeded", NULL, 0);
	}

	ctx->path[depth] = name;
	ctx->pfds[depth] = -1;
	ctx->depth = depth + 1;
}

static void outstr(struct bufout* bo, char* str)
{
	bufout(bo, str, strlen(str));
}

static void list_leaf(CTX, struct bufout* bo, char* name)
{
	int i, n = ctx->depth;
	char* root = ctx->root;
	char* pref = ctx->pref;

	outstr(bo, root);
	outstr(bo, "/");

	if(pref) {
		outstr(bo, pref);
		outstr(bo, "/");
	}

	for(i = 0; i < n; i++) {
		outstr(bo, ctx->path[i]);
		outstr(bo, "/");
	}

	outstr(bo, name);
	outstr(bo, "\n");
}

static void list_files(CTX, struct bufout* bo)
{
	byte* p = ctx->head + ctx->hoff;
	byte* e = p + ctx->hlen;

	while(p < e) {
		byte lead = *p++;
		char* name = (char*)p;

		p = skip_name(ctx, p, e);

		if(lead & TAG_DIR)
			push_dir(ctx, bo, name, lead);
		else
			list_leaf(ctx, bo, name);

	}

	ctx->depth = 0;
}

static void prep_output(CTX, struct bufout* bo)
{
	int len = PAGE;
	char* buf = heap_alloc(ctx, len);

	bufoutset(bo, STDOUT, buf, len);
}

void cmd_list(CTX)
{
	struct bufout bo;
	int n = args_left(ctx);

	setup_root(ctx, n > 1);
	take_package_arg(ctx);
	no_more_arguments(ctx);

	load_filedb(ctx);
	prep_filedb(ctx);

	prep_output(ctx, &bo);

	list_files(ctx, &bo);

	bufoutflush(&bo);
}
