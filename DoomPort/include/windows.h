#ifndef _WINDOWS_
#define _WINDOWS_

#define WIN32_LEAN_AND_MEAN

typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef int BOOL;
typedef void* HWND;
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef unsigned int UINT;
typedef long LONG;
typedef void* HANDLE;

#define MB_OK 0
#define CP_ACP 0

static inline int MultiByteToWideChar(UINT cp, DWORD flags, const char* mb, int mblen, LPWSTR wc, int wclen) { return 0; }
static inline int MessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) { return 0; }

#endif
