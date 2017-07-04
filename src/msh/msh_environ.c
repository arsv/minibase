#include <sys/pid.h>

#include <format.h>
#include <string.h>
#include <alloca.h>
#include <util.h>

#include "msh.h"

/* Since envp is presumably gets used much more often than it gets
   updated, each set or unset leaves a usable envp in the heap,
   as opposed to leaving only struct env-s and building envp on
   each exec. */

static char* stringptr(struct env* es)
{
	if(es->type == ENVPTR)
		return ((struct envptr*)es)->ref;
	if(es->type == ENVSTR)
		return es->payload;
	if(es->type == ENVLOC)
		return es->payload;

	return NULL;
}

static char* match(struct sh* ctx, char* env, char* var)
{
	char* a = env;
	char* b = var;

	while(*a == *b) {
		a++;
		b++;
	} if(*b || *a != '=')
		return NULL;

	return a + 1;
}

static int match_env(struct sh* ctx, struct env* es, char* var)
{
	return !!match(ctx, stringptr(es), var);
}

/* Until the first set or unset, the original envp from main()
   is used directly, and ctx->esep is NULL. Once there's a need
   to change envp, each ptr from the original envp is turned
   into struct envptr, but the string itself is not copied.
 
   To override or unset a variable, the old envptr gets marked ENVDEL
   and a struct env with the new string is added if necessary.
 
   The distinction between ENVSTR (inline string) and ENVPTR (pointer)
   is a kind of petty optimization to avoid copying envp strings. */

static void maybe_init_env(struct sh* ctx)
{
	char** e;
	struct envptr* p;

	if(ctx->esep)
		return;

	hrev(ctx, HEAP);

	for(e = ctx->envp; *e; e++) {
		p = halloc(ctx, sizeof(*p));
		p->len = sizeof(*p);
		p->type = ENVPTR;
		p->ref = *e;
	}

	hset(ctx, ESEP);
}

/* This function makes little sense at the first glance because
   it's just happens the common part of otherwise unrelated rebuild_envp()
   and del_env_entry(). Basically just a loop over env entries. */

typedef int (*envf)(struct sh* ctx, struct env* es, char* var);

static struct env* foreach_env(struct sh* ctx, envf func, char* var)
{
	char* ptr = ctx->heap;
	char* end = ctx->esep;
	char* p = ptr;

	while(p < end) {
		struct env* es = (struct env*) p;

		p += es->len;

		if(!es->len)
			break;
		if(es->type == ENVDEL)
			continue;
		if(func(ctx, es, var))
			return es;
	}

	return NULL;
}

static int add_env_ptr(struct sh* ctx, struct env* es, char* _)
{
	if(es->type == ENVLOC)
		return 0;

	char** ptr = halloc(ctx, sizeof(char*));
	*ptr = stringptr(es);

	return 0;
}

static void rebuild_envp(struct sh* ctx)
{
	hset(ctx, ESEP);

	foreach_env(ctx, add_env_ptr, NULL);

	char** ptr = halloc(ctx, sizeof(char*));
	*ptr = NULL;

	hset(ctx, CSEP);

	ctx->envp = (char**)ctx->esep;
}

static int del_env_entry(struct sh* ctx, char* var)
{
	struct env* es;

	if(!(es = foreach_env(ctx, match_env, var)))
		return 0;

	es->type = ENVDEL;

	return 1;
}

/* define() gets called with pkey and pval pointing into the area
   it is about to destroy, so it needs to copy the string onto its
   stack before doing hrev(). */

static char* sdup(char* str, int len, char* buf)
{
	memcpy(buf, str, len); buf[len] = '\0';
	return buf;
}

#define allocadup(str, len) sdup(str, len, alloca(len+1))

static void putvar(struct sh* ctx, char* pkey, char* pval, int type)
{
	int klen = strlen(pkey);
	int vlen = strlen(pval);
	char* key = allocadup(pkey, klen);
	char* val = allocadup(pval, vlen);

	maybe_init_env(ctx); /* pkey, pval invalid */

	hrev(ctx, ESEP);

	del_env_entry(ctx, key);

	int len = klen + vlen + 1;
	int total = sizeof(struct env) + len + 1;
	struct env* es = halloc(ctx, total);

	es->len = total;
	es->type = type;

	char* p = es->payload;
	char* e = p + len;

	p = fmtstr(p, e, key);
	p = fmtstr(p, e, "=");
	p = fmtstr(p, e, val);
	*p++ = '\0';

	rebuild_envp(ctx);
}

void define(struct sh* ctx, char* pkey, char* pval)
{
	putvar(ctx, pkey, pval, ENVLOC);
}

void setenv(struct sh* ctx, char* pkey, char* pval)
{
	putvar(ctx, pkey, pval, ENVSTR);
}

void undef(struct sh* ctx, char* pkey)
{
	int klen = strlen(pkey);
	char* key = allocadup(pkey, klen);

	maybe_init_env(ctx);

	if(!del_env_entry(ctx, key))
		return;

	hrev(ctx, ESEP);
	rebuild_envp(ctx);
}

int export(struct sh* ctx, char* var)
{
	struct env* es;

	if(!ctx->esep)
		return 0;
	if(!(es = foreach_env(ctx, match_env, var)))
		return -1;
	if(es->type != ENVLOC)
		return 0;

	es->type = ENVSTR;

	hrev(ctx, ESEP);
	rebuild_envp(ctx);

	return 0;
}

/* valueof may be called before maybe_init_env(), in which case
   it should use the original envp; or after that, then it has
   to deal with local/global stuff and thus stuct env-s. */

static char* valueof_envs(struct sh* ctx, char* var)
{
	struct env* es;
	char* p;

	if(!(es = foreach_env(ctx, match_env, var)))
		return NULL;
	if(!(p = stringptr(es)))
		return NULL;

	while(*p && *p != '=') p++;

	if(!*p) return NULL;

	return p + 1;
}

static char* valueof_orig(struct sh* ctx, char* var)
{
	char** p;
	char* r;

	for(p = ctx->envp; *p; p++)
		if((r = match(ctx, *p, var)))
			return r;

	return NULL;
}

static char* valueof_argv(struct sh* ctx, int i)
{
	if(i < 0)
		return NULL;
	if(i == 0)
		return ctx->topargv[0];
	if(i + ctx->topargp <= ctx->topargc)
		return ctx->topargv[ctx->topargp + i - 1];
	return NULL;
}

static char* valueof_pid(struct sh* ctx)
{
	if(*ctx->pid)
		goto out;

	int pid = sys_getpid();
	char* p = ctx->pid;
	char* e = ctx->pid + sizeof(ctx->pid) - 1;

	p = fmtint(p, e, pid);
	*p = '\0';
out:
	return ctx->pid;
}

static char* valueof_spec(struct sh* ctx, char* var)
{
	char c = *var;

	if(c >= '0' && c <= '9')
		return valueof_argv(ctx, c - '0');
	if(c == '$')
		return valueof_pid(ctx);

	return NULL;
}

static int special(char* var)
{
	if(!var[0] || var[1])
		return 0;

	char c = var[0];

	if(c >= '0' || c <= '1')
		return 1;
	if(c == '$')
		return 1;

	return 0;
}

char* valueof(struct sh* ctx, char* var)
{
	if(special(var))
		return valueof_spec(ctx, var);
	if(ctx->esep)
		return valueof_envs(ctx, var);
	else
		return valueof_orig(ctx, var);
}
