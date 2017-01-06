#include <string.h>
#include <format.h>

#include "msh.h"

#define SEP 0
#define ARG 1
#define DQUOT 2
#define SQUOT 3
#define SLASH 4
#define VSIGN 5
#define VCONT 6
#define COMM 7

static void dispatch(struct sh* ctx, char c);

static void set_state(struct sh* ctx, int st)
{
	ctx->state = (ctx->state & ~0xFF) | (st & 0xFF);
}

static void push_state(struct sh* ctx, int st)
{
	ctx->state = (ctx->state << 8) | (st & 0xFF);
}

static void pop_state(struct sh* ctx)
{
	ctx->state = (ctx->state >> 8);
}

static void start_var(struct sh* ctx)
{
	hset(ctx, VSEP);
	push_state(ctx, VSIGN);
}

static void end_var(struct sh* ctx)
{
	*(ctx->hptr) = '\0';	

	char* val = valueof(ctx, ctx->var);
	long vlen = strlen(val);

	hrev(ctx, VSEP);

	char* spc = halloc(ctx, vlen);
	memcpy(spc, val, vlen);

	pop_state(ctx);
}

static void add_char(struct sh* ctx, char c)
{
	char* spc = halloc(ctx, 1);
	*spc = c;
}

static void end_arg(struct sh* ctx)
{
	add_char(ctx, 0);
	ctx->count++;
	set_state(ctx, SEP);
}

static void end_cmd(struct sh* ctx)
{
	int argn = ctx->count;
	char* base = ctx->csep;
	char* bend = ctx->hptr;

	if(!argn) return;

	char** argv = halloc(ctx, (argn+1)*sizeof(char*));
	int argc = 0;

	argv[0] = ctx->csep;
	char* p;

	int sep = 1;
	for(p = base; p < bend; p++) {
		if(sep && argc < argn)
			argv[argc++] = p;
		sep = !*p;
	}

	argv[argc] = NULL;

	exec(ctx, argc, argv); /* may damage heap, argv, csep! */

	hrev(ctx, CSEP);
	ctx->count = 0;
}

static void parse_sep(struct sh* ctx, char c)
{
	switch(c) {
		case '\0':
		case ' ':
		case '\t':
			break;
		case '#':
			end_cmd(ctx);
			set_state(ctx, COMM);
			break;
		case '\n':
			end_cmd(ctx);
			set_state(ctx, SEP);
			break;
		default:
			set_state(ctx, ARG);
			dispatch(ctx, c);
	};
}

static void parse_arg(struct sh* ctx, char c)
{
	switch(c) {
		case '\0':
		case ' ':
		case '\t': end_arg(ctx); break;
		case '\n': end_arg(ctx); end_cmd(ctx); break;
		case '$':  start_var(ctx); break;
		case '"':  push_state(ctx, DQUOT); break;
		case '\'': push_state(ctx, SQUOT); break;
		case '\\': push_state(ctx, SLASH); break;
		default: add_char(ctx, c);
	}
}

static void parse_dquote(struct sh* ctx, char c)
{
	switch(c) {
		case '"': pop_state(ctx); break;
		case '$': start_var(ctx); break;
		case '\\': push_state(ctx, SLASH); break;
		default: add_char(ctx, c);
	}
}

static void parse_squote(struct sh* ctx, char c)
{
	switch(c) {
		case '\'': pop_state(ctx); break;
		default: add_char(ctx, c);
	}
}

static void parse_slash(struct sh* ctx, char c)
{
	switch(c) {
		case '\n': c = ' '; break;
		case 't': c = '\t'; break;
		case 'n': c = '\n'; break;
	};
	
	add_char(ctx, c);
	pop_state(ctx);
}

static void parse_vsign(struct sh* ctx, char c)
{
	switch(c) {
		case 'a'...'z':
		case 'A'...'Z':
			add_char(ctx, c);
			set_state(ctx, VCONT);
			break;
		case '"':
		case '\'':
		case '\\':
		case '$':
		case '\0':
		case '\n':
			fatal(ctx, "invalid syntax", NULL);
		default:
			add_char(ctx, c);
			end_var(ctx);
	}
}

static void parse_comm(struct sh* ctx, char c)
{
	if(c == '\n')
		set_state(ctx, SEP);
}

static void parse_vcont(struct sh* ctx, char c)
{
	switch(c) {
		case 'a'...'z':
		case 'A'...'Z':
			add_char(ctx, c);
			break;
		default:
			end_var(ctx);
			dispatch(ctx, c);
	}
}

static void dispatch(struct sh* ctx, char c)
{
	switch(ctx->state & 0xFF) {
		case SEP: parse_sep(ctx, c); break;
		case ARG: parse_arg(ctx, c); break;
		case DQUOT: parse_dquote(ctx, c); break;
		case SQUOT: parse_squote(ctx, c); break;
		case SLASH: parse_slash(ctx, c); break;
		case VSIGN: parse_vsign(ctx, c); break;
		case VCONT: parse_vcont(ctx, c); break;
		case COMM: parse_comm(ctx, c); break;
		default: fatal(ctx, "bad internal state", NULL);
	}
}

void parse(struct sh* ctx, char* buf, int len)
{
	char* end = buf + len;
	char* p;

	for(p = buf; p < end; p++) {
		dispatch(ctx, *p);
		if(*p == '\n')
			ctx->line++;
	}
}

void pfini(struct sh* ctx)
{
	dispatch(ctx, '\0');

	if(ctx->state)
		fatal(ctx, "unexpected EOF", NULL);
}
