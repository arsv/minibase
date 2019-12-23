#!/bin/sh

cat <<END
#ifndef CMD
#define CMD(name) void cmd_##name(struct sh* ctx);
#endif

END

sed -ne 's/^void cmd_\([a-z]\+\).*/CMD(\1)/p' msh_cmd_*.c | sort

cat <<END

#undef CMD
END
