struct sh;

#define CMD(name) int cmd_##name(struct sh* ctx, int argc, char** argv)

/* cmd_ifelse.c */
CMD(if);
CMD(elif);
CMD(else);
CMD(fi);

/* cmd_proc.c */
CMD(exec);
CMD(exit);
CMD(cd);
CMD(chroot);
CMD(setprio);
CMD(rlimit);
CMD(seccomp);

/* cmd_uidgid.c */
CMD(setuid);
CMD(setgid);
CMD(groups);

/* cmd_file.c */
CMD(echo);
CMD(warn);
CMD(unset);
CMD(open);
CMD(close);
CMD(dupfd);
CMD(setcg);
