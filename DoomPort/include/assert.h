#ifndef _ASSERT_H_
#define _ASSERT_H_
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((x) ? (void)0 : (void)0)
#endif
#endif
