#ifndef CMD
#define CMD(name) int cmd_##name(struct sh* ctx);
#endif

CMD(cd)
CMD(exec)
CMD(exit)
CMD(echo)
CMD(warn)
CMD(die)
CMD(setenv)
CMD(unset)
CMD(export)
CMD(open)
CMD(dupfd)
CMD(close)
CMD(write)
CMD(unlink)
CMD(mkdirs)
CMD(rlimit)
CMD(seccomp)
CMD(setprio)
CMD(umask)
CMD(chroot)
CMD(secbits)
CMD(setcaps)
CMD(setcg)
CMD(sleep)
CMD(setuid)
CMD(setgid)
CMD(groups)

#undef CMD
