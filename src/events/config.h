/* Edge-triggering global system actions may be a bit too sensitive
   in some cases, like an accidental Power button click starting
   a system shutdown. To mend this, allow timed events: the action
   gets triggered once the key has been held for some time.

   The default time is chosen so that it's long enough to distinguish
   it from a simple keypress, and short enough to not interact with
   possible firmware actions (hold Power for ~3s to cut power).

   There's also at least one case where a long timeout is preferable:
   triggering sleep if the lid is kept closed for some amount of time.
   One second is way too short in this case, for let's make it 10 seconds.

   Making either constant configuration likely makes little sense, not
   nearly enough to justify the complexity. */

#define HOLDTIME   1000 /* 1s in ms; for keys */
#define LONGTIME  10000 /* 10s in ms; for switches */

#ifdef DEVEL
# define CONFDIR "./etc/keymon"
# define CONFIG "./etc/keymon.conf"
#else
# define CONFDIR "/etc/keymon"
# define CONFIG "/etc/keymon.conf"
#endif
