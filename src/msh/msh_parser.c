#include <sys/creds.h>

#include <string.h>
#include <format.h>

#include "msh.h"
#include "msh_cmd.h"

/* Parsing code splits incoming stream into argv[]s and calls
   relevant cmd_* handlers. */

#define NIL    0x0    /* .             start-of-line                    */
#define SEP    0x1    /* foo .         inter-token context              */
#define ARG    0x2    /* echo Hel.     unquoted argument context        */

#define NIBSL  0x3    /* \.            NIL state backslash              */
#define SEBSL  0x4    /* echo \.       SEP state backslash              */
#define ARBSL  0x5    /* echo fo\.     ARG state backslash              */

#define VSIGN  0x6    /* echo $.       first char of var reference      */
#define VCONT  0x7    /* echo $fo.     non-first char of var reference  */
#define COMMT  0x8    /* # some co.    commented-out context            */

#define DQUOT  0x9    /* "He.          double-quoted context            */
#define DQBSL  0xa    /* "\.           double-quoted backslash          */
#define DQVSG  0xb    /* "$.           double-quoted var 1st char       */
#define DQVCN  0xc    /* "$a.          double-quoted var nth char       */

#define SQUOT  0xd    /* 'He.          single-quoted context            */
#define SQBSL  0xe    /* '\.           single-quoted backslash          */

#define TRAIL  0xf    /* "foo".        after a quoted context           */

/* Command lookup code */

static const struct cmd {
	char name[12];
	void (*func)(CTX);
} builtins[] = {
#define CMD(name) \
	{ #name, cmd_##name },
#include "msh_cmd.h"
};

static const struct cmd* builtin(const char* name)
{
	const struct cmd* cc;
	int maxlen = sizeof(cc->name);

	for(cc = builtins; cc < ARRAY_END(builtins); cc++)
		if(!strncmp(cc->name, name, maxlen))
			return cc;

	return NULL;
}

static void command(CTX, int argc, char** argv)
{
	const struct cmd* cc;

	if(argc <= 0) return;

	ctx->argc = argc;
	ctx->argp = 1;
	ctx->argv = argv;

	char* lead = argv[0];

	if(!(cc = builtin(lead)))
		fatal(ctx, "unknown command", lead);

	cc->func(ctx);

	ctx->argc = 0;
	ctx->argv = NULL;
}

/* ARGV assembly */

static char* advance(char* p, char* e)
{
	for(; p < e; p++)
		if(!*p) return p;

	return NULL;
}

static int count_args(CTX, char* p, char* e)
{
	int count = 0;

	while(p < e) {
		char* q = advance(p, e);

		if(!q) break;

		count++;

		p = q + 1;
	}

	return count;
}

static void index_args(CTX, char* p, char* e, char** argv, int argc)
{
	int i = 0;

	while(p < e) {
		char* q = advance(p, e);

		if(!q || i >= argc) break;

		argv[i++] = p;

		p = q + 1;
	}

	argv[i] = NULL;
}

static void execute(CTX)
{
	void* p = ctx->asep;
	void* e = ctx->hptr;

	int argc = count_args(ctx, p, e);
	int size = (argc+1)*sizeof(char*);

	if(!argc) return;

	char** argv = heap_alloc(ctx, size);

	index_args(ctx, p, e, argv, argc);

	command(ctx, argc, argv);

	ctx->hptr = ctx->asep;
}

/* Character classification */

static int isspace(char c)
{
	return (c == ' ' || c == '\t' || c == '\n');
}

static int isalpha(char c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static int isnumer(char c)
{
	return (c >= '0' && c <= '9');
}

static int isalnum(char c)
{
	return isalpha(c) || isnumer(c);
}

/* Heap string assembly */

static void push_char(CTX, char c)
{
	char* ptr = ctx->hptr;
	char* end = ctx->hend;

	if(ptr >= end)
		heap_extend(ctx);

	*ptr++ = c;

	ctx->hptr = ptr;
}

static void push_esc(CTX, char c)
{
	if(c == 't')
		c = '\t';
	else if(c == 'n')
		c = '\n';

	push_char(ctx, c);
}

static void push_end(CTX)
{
	push_char(ctx, '\0');
}

static void push_var(CTX, char c)
{
	ctx->var = ctx->hptr;

	push_char(ctx, c);
}

/* Variable substitions */

static char* valueof(CTX, char* name)
{
	int nlen = strlen(name);
	struct env* ev;
	char* var;

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev)) {
		if(!(var = env_value(ctx, ev, EV_SVAR)))
			continue;

		if(strncmp(var, name, nlen))
			continue;
		if(var[nlen] != '=')
			continue;

		return var + nlen + 1;
	}

	return NULL;
}

static void subst_var(CTX)
{
	char* var = ctx->var;

	if(!var) fatal(ctx, "internal var end w/o var", NULL);

	push_end(ctx);

	char* val = valueof(ctx, var);

	if(!val) fatal(ctx, "undefined variable", var);

	ctx->hptr = var;

	long vlen = strlen(val);
	char* spc = heap_alloc(ctx, vlen);

	memcpy(spc, val, vlen);
}

static void subst_special(CTX, char c)
{
	if(c == '$') {
		FMTBUF(p, e, buf, 32);
		p = fmtint(p, e, sys_getpid());
		FMTEND(p, e);

		int len = p - buf;
		char* spc = heap_alloc(ctx, len);

		memcpy(spc, buf, len);
	} else {
		FMTBUF(p, e, buf, 8);
		p = fmtchar(p, e, '$');
		p = fmtchar(p, e, c);
		FMTEND(p, e);

		fatal(ctx, "undefined special variable", buf);
	}
}

/* State handlers */

static int parse_nil(CTX, char c)
{
	if(c == '\n')
		return NIL;
	if(isspace(c))
		return NIL;
	if(c == '#')
		return COMMT;
	if(c == '\\')
		return NIBSL;
	if(c == '"' || c == '\'')
		fatal(ctx, "quoted command", NULL);
	if(c == '$')
		fatal(ctx, "indirect command", NULL);

	push_char(ctx, c);

	return ARG;
}

static int parse_sep(CTX, char c)
{
	if(c == '\n') {
		execute(ctx);
		return NIL;
	} else if(c == '#') {
		execute(ctx);
		return COMMT;
	}
	if(isspace(c))
		return SEP;
	if(c == '\\')
		return SEBSL;
	if(c == '"')
		return DQUOT;
	if(c == '\'')
		return SQUOT;
	if(c == '$')
		return VSIGN;

	push_char(ctx, c);

	return ARG;
}

static int parse_arg(CTX, char c)
{
	if(c == '$') {
		return VSIGN;
	} if(c == '\\') {
		return ARBSL;
	} if(c == '#') {
		push_end(ctx);
		execute(ctx);
		return COMMT;
	} if(c == '\n') {
		push_end(ctx);
		execute(ctx);
		return NIL;
	} if(isspace(c)) {
		push_end(ctx);
		return SEP;
	} if(c == '"' || c == '\'') {
		fatal(ctx, "misplaced quotes", NULL);
	}

	push_char(ctx, c);

	return ARG;
}

static int parse_nibsl(CTX, char c)
{
	if(c == '\n')
		return NIL;

	push_esc(ctx, c);

	return NIL;
}

static int parse_sebsl(CTX, char c)
{
	if(c == '\n')
		return SEP;

	push_esc(ctx, c);

	return ARG;
}

static int parse_arbsl(CTX, char c)
{
	push_esc(ctx, c);

	return ARG;
}

static int parse_vsign(CTX, char c)
{
	if(isalpha(c)) {
		push_var(ctx, c);
		return VCONT;
	} else if(!isspace(c)) {
		subst_special(ctx, c);
		return ARG;
	}

	fatal(ctx, "missing variable name", NULL);
}

static int parse_vcont(CTX, char c)
{
	if(isalnum(c)) {
		push_char(ctx, c);
		return VCONT;
	} else {
		subst_var(ctx);
		return parse_arg(ctx, c);
	}
}

static int parse_commt(CTX, char c)
{
	if(c == '\n')
		return SEP;

	return COMMT;
}

static int parse_dquot(CTX, char c)
{
	if(c == '"') {
		push_end(ctx);
		return TRAIL;
	} else if(c == '$') {
		return DQVSG;
	} else if(c == '\\') {
		return DQBSL;
	} else {
		push_char(ctx, c);
		return DQUOT;
	}
}

static int parse_dqvsg(CTX, char c)
{
	if(isalpha(c)) {
		push_var(ctx, c);
		return DQVCN;
	} else if(!isspace(c)) {
		subst_special(ctx, c);
		return DQUOT;
	}

	fatal(ctx, "missing variable name", NULL);
}

static int parse_dqvcn(CTX, char c)
{
	if(isalnum(c)) {
		push_char(ctx, c);
		return DQVCN;
	} else {
		subst_var(ctx);
		return parse_dquot(ctx, c);
	}
}

static int parse_dqbsl(CTX, char c)
{
	if(c == '\n')
		;
	else push_esc(ctx, c);

	return DQUOT;
}

static int parse_squot(CTX, char c)
{
	if(c == '\\')
		return SQBSL;
	if(c == '\'') {
		push_end(ctx);
		return TRAIL;
	} else {
		push_char(ctx, c);
		return SQUOT;
	}
}

static int parse_sqbsl(CTX, char c)
{
	if(c == '\n')
		;
	else push_esc(ctx, c);

	return SQUOT;
}

static int parse_trail(CTX, char c)
{
	if(c == '\n') {
		execute(ctx);
		return NIL;
	} else if(c == '#') {
		execute(ctx);
		return COMMT;
	} else if(isspace(c)) {
		return SEP;
	}

	fatal(ctx, "trailing characters after quote", NULL);
}

typedef int (*handler)(CTX, char c);

static const handler table[] = {
	[NIL  ] = parse_nil,
	[SEP  ] = parse_sep,
	[ARG  ] = parse_arg,

	[NIBSL] = parse_nibsl,
	[SEBSL] = parse_sebsl,
	[ARBSL] = parse_arbsl,

	[VSIGN] = parse_vsign,
	[VCONT] = parse_vcont,
	[COMMT] = parse_commt,

	[DQUOT] = parse_dquot,
	[DQBSL] = parse_dqbsl,
	[DQVSG] = parse_dqvsg,
	[DQVCN] = parse_dqvcn,

	[SQUOT] = parse_squot,
	[SQBSL] = parse_sqbsl,

	[TRAIL] = parse_trail
};

static void dispatch(CTX, char c)
{
	unsigned state = ctx->state;

	if(state >= ARRAY_SIZE(table))
		fatal(ctx, "internal state error", NULL);

	handler cc = table[state];

	if(!cc) fatal(ctx, "internal state error", NULL);

	ctx->state = cc(ctx, c);
}

/* Entry points */

void parse(CTX, char* buf, int len)
{
	char* end = buf + len;
	char* p;

	for(p = buf; p < end; p++) {
		char c = *p;

		if(!c) fatal(ctx, "stray NUL in script", NULL);

		dispatch(ctx, c);

		if(c == '\n') ctx->line++;
	}
}

void parse_finish(CTX)
{
	if(ctx->asep != ctx->hptr)  /* incomplete args in heap */
		;
	else if(ctx->state != NIL)  /* unfinished $ref or quote */
		;
	else return; /* proper end of input */

	fatal(ctx, "unexpected EOF", NULL);
}
