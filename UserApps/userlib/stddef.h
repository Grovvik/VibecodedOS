#ifndef _STDDEF_H
#define _STDDEF_H

#include "userlib.h"

#ifndef offsetof
#define offsetof(s,m) ((size_t)&(((s*)0)->m))
#endif

typedef long long ptrdiff_t;
typedef unsigned short wchar_t;

#endif
