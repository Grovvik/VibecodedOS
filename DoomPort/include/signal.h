#ifndef _SIGNAL_H_
#define _SIGNAL_H_
#define SIGINT 2
#define SIGABRT 6
#define SIGTERM 15
typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t handler);
int raise(int sig);
#endif
