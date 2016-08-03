// Compiler shims

#if defined(_MSC_VER)

#include <io.h>
#define UNREACHABLE __assume(0)
#define changesize(fd,size) _chsize(fd,size)

#elif defined(__GNUC__)

#include <unistd.h>
#define __debugbreak abort
#define UNREACHABLE __builtin_unreachable()
#define changesize(fd,size) ftruncate(fd,size)

#endif

#ifndef __cdecl
	#if defined(_WIN64) || defined(__x86_64__) || defined(__ppc64__)
	#define __cdecl
	#elif defined(__GNUC__)
	#define __cdecl __attribute__((__cdecl__))
	#endif
#endif
