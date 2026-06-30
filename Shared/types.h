#ifndef _SHARED_TYPES_H_
#define _SHARED_TYPES_H_

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef signed char      i8;
typedef signed short     i16;
typedef signed int       i32;
typedef signed long long i64;

typedef u64 usize;
typedef i64 isize;

typedef u32 ntstatus;

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define STATIC static
#define VOID void
#define IN
#define OUT
#define OPTIONAL

#define CONTAINING_RECORD(address, type, field) \
    ((type*)((u8*)(address) - (u8*)(&((type*)0)->field)))

#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12

#endif
