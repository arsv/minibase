#ifndef CMD
#define CMD(name) int cmd_##name(struct sh* ctx, int argc, char** argv);
#endif

CMD(open)
CMD(dupfd)
CMD(close)
CMD(echo)
CMD(warn)
CMD(rlimit)
CMD(seccomp)
CMD(setprio)
CMD(umask)
CMD(chroot)
CMD(cd)
CMD(exec)
CMD(exit)
CMD(setcg)
CMD(unset)
CMD(sleep)
CMD(setuid)
CMD(setgid)
CMD(groups)

#undef CMD
