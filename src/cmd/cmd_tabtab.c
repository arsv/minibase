#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <printf.h>

#include "cmd.h"

void single_tab(CTX)
{

}

void double_tab(CTX)
{
	struct tabtab* tt = &ctx->tts;

	if(!tt->buf)
		return;

}

void cancel_tab(CTX)
{
	struct tabtab* tt = &ctx->tts;

	ctx->tab = 0;

	if(!tt->buf)
		return;

	sys_munmap(tt->buf, tt->size);

	memzero(tt, sizeof(*tt));
}
