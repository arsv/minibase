#include <bits/types.h>

int deutf(char* buf, long len, uint* out);

long skip_left(char* buf, long len);
long skip_right(char* buf, long len);
long skip_left_until_space(char* buf, long len);

long visual_width(char* buf, long len);
long skip_right_visually(char* buf, long len, long vlen);
long skip_left_visually(char* buf, long len, long vlen);
