#ifndef _CTYPE_H
#define _CTYPE_H

#define isdigit(c)  ((c) >= '0' && (c) <= '9')
#define isspace(c)  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == '\v' || (c) == '\f')
#define isprint(c)  ((c) >= 32 && (c) < 127)
#define isxdigit(c) (isdigit(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#define tolower(c)  (((c) >= 'A' && (c) <= 'Z') ? (c) + 32 : (c))
#define toupper(c)  (((c) >= 'a' && (c) <= 'z') ? (c) - 32 : (c))

#endif
