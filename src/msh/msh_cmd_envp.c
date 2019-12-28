#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

/* The problem with commands operating on the env-s is that they
   have their argv[] in the area they need to write. Heap layout
   at entry here looks like this:

       env env ... env arg arg ... arg argv

   Commands like set or setenv have to copy one (or more) of their
   args between the last env and the first arg, but there is no free
   space there.

   So the trick used here is to make copies of the necessary args
   above the original arg area like so:

       env env ... env arg arg ... arg argv tmp tmp ... tmp

   and then proceed with env insertion, overwriting the old args:

       env env ... env env ~~~ ... ~~~ ~~~~ tmp tmp ... tmp

   The area freed by dropping *all* args is always large enough
   because each argv[] pointer is at least as large as struct env
   header.

   Once cmd_ handler is done, execute() will reset hptr to asep:

      env env ... env env

   so we don't have to worry about tmp-s much here. */

static char* realloc_higher(CTX, char* str)
{
	int len = strlen(str);
	int need = (len + 1 + 3) & ~3;

	char* new = heap_alloc(ctx, need);

	memcpy(new, str, len + 1);

	return new;
}

static char* realloc_pair(CTX, char* key, char* val)
{
	int klen = strlen(key);
	int vlen = strlen(val);
	int need = (klen + vlen + 2);

	need = (need + 3) & ~3;

	char* new = heap_alloc(ctx, need);

	char* p = new;

	memcpy(p, key, klen); p += klen;
	*p++ = '=';
	memcpy(p, val, vlen); p += vlen;
	*p = '\0';

	return new;
}

static int named(char* var, char* name)
{
	char* v = var;
	char* n = name;

	while(*n && *v == *n) {
		n++;
		v++;
	}
	if(*n || *v != '=')
		return 0;

	return 1;
}

static void check_valid_name(CTX, char* name)
{
	char* p = name;
	char c;

	while((c = *p++)) {
		if(c == '=')
			fatal(ctx, "invalid variable name:", name);
	}
}

static void check_not_defined(CTX, char* name)
{
	struct env* ev;
	char* var;

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev)) {
		if(!(var = env_value(ctx, ev, EV_SVAR)))
			continue;
		else if(!named(var, name))
			continue;
		else
			fatal(ctx, "duplicate definition", NULL);
	}
}

static void clear_definition(CTX, char* name)
{
	struct env* ev;
	char* var;

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev)) {
		if(!(var = env_value(ctx, ev, EV_ENVP)))
			continue;
		else if(!named(var, name))
			continue;

		ev->key &= ~(EV_ENVP | EV_SVAR);
	}
}

static void clear_all_entries(CTX, int type)
{
	struct env* ev;

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev)) {
		int key = ev->key;

		if(!(key & type)) continue;

		ev->key = key & ~type;
	}
}

static void append_variable(CTX, char* var, int type)
{
	int vlen = strlen(var);

	void* asep = ctx->asep;
	struct env* ev = asep;
	int size = sizeof(*ev) + vlen + 1;

	size = (size + 3) & ~3;

	ctx->asep = asep + size;

	if(size > EV_SIZE)
		fatal(ctx, "variable overflow", NULL);

	ev->key = type | size;

	memcpy(ev->payload, var, vlen + 1);
}

static void append_reference(CTX, int idx, int type)
{
	void* asep = ctx->asep;
	struct env* ev = asep;
	int size = sizeof(*ev);

	ctx->asep = asep + size;

	if(idx > EV_SIZE)
		fatal(ctx, "reference overflow", NULL);

	ev->key = EV_REF | type | idx;
}

static void reserve_orig_envp(CTX)
{
	if(ctx->customenvp >= 0)
		return;

	char** p = ctx->environ;
	int count = 0;

	while(*p++) count++;

	int size = (count+1)*sizeof(struct env);

	(void)heap_alloc(ctx, size);
}

static void start_custom_envp(CTX)
{
	if(ctx->customenvp >= 0)
		return;

	char** envp = ctx->environ;
	char* var;
	int i = 0;

	for(; (var = envp[i]); i++)
		append_reference(ctx, i, EV_ENVP);

	ctx->customenvp = i;
}

void cmd_set(CTX)
{
	char* var = shift(ctx);
	char* val = shift(ctx);

	no_more_arguments(ctx);

	check_valid_name(ctx, var);
	check_not_defined(ctx, var);

	char* pair = realloc_pair(ctx, var, val);

	append_variable(ctx, pair, EV_SVAR);
}

void cmd_setenv(CTX)
{
	char* var = shift(ctx);
	char* val = shift(ctx);

	no_more_arguments(ctx);

	check_valid_name(ctx, var);
	reserve_orig_envp(ctx);

	char* key = realloc_higher(ctx, var);
	char* pair = realloc_pair(ctx, var, val);

	start_custom_envp(ctx);
	clear_definition(ctx, key);
	append_variable(ctx, pair, EV_ENVP);
}

void cmd_getenv(CTX)
{
	char* name = shift(ctx);

	no_more_arguments(ctx);

	check_valid_name(ctx, name);
	check_not_defined(ctx, name);

	char** envp = ctx->environ;
	int i = 0;
	char* var;

	for(; (var = envp[i]); i++) {
		if(named(var, name))
			goto got;
	}

	fatal(ctx, "undefined variable:", name);
got:
	append_reference(ctx, i, EV_SVAR);
}

void cmd_delenv(CTX)
{
	char* name = shift(ctx);

	no_more_arguments(ctx);

	check_valid_name(ctx, name);

	reserve_orig_envp(ctx);

	name = realloc_higher(ctx, name);

	start_custom_envp(ctx);

	clear_definition(ctx, name);
}

void cmd_clearenv(CTX)
{
	no_more_arguments(ctx);

	if(ctx->customenvp < 0)
		ctx->customenvp = 0;
	else
		clear_all_entries(ctx, EV_ENVP);
}

void cmd_onexit(CTX)
{
	need_some_arguments(ctx);

	int i, narg = ctx->argc - ctx->argp;
	char** args = ctx->argv + ctx->argp;
	int size = (narg+1)*sizeof(char*);

	char** temp = heap_alloc(ctx, size);

	clear_all_entries(ctx, EV_TRAP);

	for(i = 0; i < narg; i++)
		temp[i] = realloc_higher(ctx, args[i]);
	for(i = 0; i < narg; i++)
		append_variable(ctx, temp[i], EV_TRAP);
}
