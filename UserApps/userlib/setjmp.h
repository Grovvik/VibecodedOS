#ifndef _SETJMP_H_
#define _SETJMP_H_

typedef unsigned long long jmp_buf[16];

int _setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#define setjmp(env) _setjmp(env)

#endif
