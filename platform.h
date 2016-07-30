// Compiler shims

#if defined(_MSC_VER)

#define UNREACHABLE __assume(0)

#elif defined(__GNUC__)

#define __debugbreak abort
#define UNREACHABLE __builtin_unreachable()

#endif

#ifndef __cdecl
	#if defined(_WIN64) || defined(__x86_64__) || defined(__ppc64__)
	#define __cdecl
	#elif defined(__GNUC__)
	#define __cdecl __attribute__((__cdecl__))
	#endif
#endif
