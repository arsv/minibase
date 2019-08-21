#include <cdefs.h>
#include <printf.h>

#include "ifmon.h"

void link_next(LS, int state)
{
	ls->state = state;
	ls->flags |= LF_TOUCHED;
	tracef("link %s state %x\n", ls->name, state);
}

void script_exit(LS, int status)
{
	int state = ls->state;

	switch(state) {
		case LS_SPAWN_MODE:
			tracef("mode exit %s %i\n", ls->name, status);
			return link_next(ls, LS_IDLE);
		case LS_SPAWN_STOP:
			tracef("stop exit %s %i\n", ls->name, status);
			return link_next(ls, LS_DROP);
		default:
			tracef("script_exit %s %i\n", ls->name, status);
	}
}

static void act_spawn_mode(LS)
{
	int ret;
	
	if((ret = spawn_mode(ls)) >= 0)
		return; /* wait for results */

	link_next(ls, LS_IDLE);
}

static void act_spawn_stop(LS)
{
	int ret;
	
	if((ret = spawn_stop(ls)) >= 0)
		return; /* wait for results */

	link_next(ls, LS_IDLE);
}

static const struct action {
	int state;
	void (*call)(LS);
} actions[] = {
	{ LS_SPAWN_MODE, act_spawn_mode },
	{ LS_SPAWN_STOP, act_spawn_stop }
};

static const struct action* locate_action_for(LS)
{
	int state = ls->state;	
	const struct action* act;

	for(act = actions; act < ARRAY_END(actions); act++)
		if(act->state == state)
			return act;

	return NULL;
}

void reassess_link(LS)
{
	const struct action* act;
	int n = 0;

	while(ls->flags & LF_TOUCHED) {
		ls->flags &= ~LF_TOUCHED;

		if(n++ > 10) /* safety */
			ls->state = LS_IDLE;

		if(!(act = locate_action_for(ls)))
			break;

		act->call(ls);
	}

	if(ls->state == LS_DROP)
		free_link_slot(ls);
}

void check_links(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		reassess_link(ls);
}
