#ifndef _STRPTIME_H
#define _STRPTIME_H
#include <time.h>

extern "C" char * strptime(const char *buf, const char *fmt, struct tm *tm);

#endif // _STRPTIME_H