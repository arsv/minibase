#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <output.h>
#include <format.h>
#include <main.h>
#include <util.h>

#include "mpac.h"

/* Archive contents listing.

   There are two way to do it, `list` outputs proper pathnames similar
   to `tar t`, while `dump` formats archive index without much processing
   in the raw depth-basename form. */

static void flush_output(CTX)
{
	struct bufout* bo = ctx->bo;

	if(!bo) return;

	bufoutflush(bo);
}

void quit(CTX, const char* msg, char* arg, int ret)
{
	flush_output(ctx);

	fail(msg, arg, ret);
}

static void output(CTX, char* buf, int len)
{
	struct bufout* bo = ctx->bo;

	bufout(bo, buf, len);
}

static void outstr(CTX, char* str)
{
	output(ctx, str, strlen(str));
}

static void list_file(CTX)
{
	int i, n = ctx->depth;

	for(i = 0; i < n; i++) {
		outstr(ctx, ctx->path[i]);
		output(ctx, "/", 1);
	}

	outstr(ctx, ctx->name);
	output(ctx, "\n", 1);
}

static void enter_dir(CTX, byte lead)
{
	int depth = ctx->depth;
	int lvl = lead & TAG_DEPTH;

	if(lvl > depth)
		fail("invalid index entry", NULL, 0);

	char* name = ctx->name;

	if(depth >= MAXDEPTH)
		quit(ctx, "tree depth exceeded", NULL, 0);

	ctx->path[depth] = name;
	ctx->depth = lvl + 1;
}

static void list_entries(CTX)
{
	int lead;

	while((lead = next_entry(ctx)) >= 0) {
		if((lead & TAG_DIR)) {
			enter_dir(ctx, lead);
		} else {
			list_file(ctx);
		}
	}

	ctx->depth = 0;
}

static void dump_entries(CTX)
{
	int lead;
	char buf[1024];

	while((lead = next_entry(ctx)) >= 0) {
		int type = lead & TAG_TYPE;

		char* p = buf;
		char* e = buf + sizeof(buf) - 1;

		if(lead & TAG_DIR) {
			p = fmtint(p, e, lead & TAG_DEPTH);
		} else {
			if(type == TAG_LINK)
				p = fmtstr(p, e, "~");
			else if(type == TAG_FILE)
				p = fmtstr(p, e, "-");
			else if(type == TAG_EXEC)
				p = fmtstr(p, e, "+");
			else
				p = fmtstr(p, e, "?");
		}

		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, ctx->name);

		if(!(lead & TAG_DIR)) {
			p = fmtstr(p, e, " (");
			p = fmtint(p, e, ctx->size);
			p = fmtstr(p, e, ")");
		}

		*p++ = '\n';

		output(ctx, buf, p - buf);
	}
}

static void prep_output(CTX, struct bufout* bo)
{
	int outlen = PAGE;
	void* outbuf = heap_alloc(ctx, outlen);

	bufoutset(bo, STDOUT, outbuf, outlen);

	ctx->bo = bo;
}

void cmd_list(CTX)
{
	struct bufout bo;

	char* name = shift(ctx);

	no_more_arguments(ctx);

	open_pacfile(ctx, name);
	heap_init(ctx, 2*PAGE);

	prep_output(ctx, &bo);

	load_index(ctx);
	list_entries(ctx);

	flush_output(ctx);
}

void cmd_dump(CTX)
{
	struct bufout bo;

	char* name = shift(ctx);

	no_more_arguments(ctx);

	open_pacfile(ctx, name);
	heap_init(ctx, 2*PAGE);

	prep_output(ctx, &bo);

	load_index(ctx);
	dump_entries(ctx);

	flush_output(ctx);
}
