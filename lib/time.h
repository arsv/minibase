#ifndef __TIME_H__
#define __TIME_H__

#include <bits/time.h>

void tm2tv(const struct tm* tm, struct timeval* tv);
void tv2tm(const struct timeval* tv, struct tm* tm);

#endif
