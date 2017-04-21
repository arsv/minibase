#!/bin/sh

cat <<END
#ifndef CMD
#define CMD(name) int cmd_##name(struct sh* ctx);
#endif

END

sed -ne 's/^int cmd_\([a-z]\+\).*/CMD(\1)/p' msh_cmd_*.c

cat <<END

#undef CMD
END
