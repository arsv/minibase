#ifndef CMD
#define CMD(name) int cmd_##name(struct sh* ctx, int argc, char** argv);
#endif

CMD(cd)
CMD(exec)
CMD(exit)
CMD(echo)
CMD(warn)
CMD(die)
CMD(open)
CMD(dupfd)
CMD(close)
CMD(rlimit)
CMD(seccomp)
CMD(setprio)
CMD(umask)
CMD(chroot)
CMD(secbits)
CMD(setcaps)
CMD(setcg)
CMD(setenv)
CMD(unset)
CMD(export)
CMD(sleep)
CMD(setuid)
CMD(setgid)
CMD(groups)

#undef CMD
