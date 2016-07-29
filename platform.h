// Compiler shims

#if defined(_MSC_VER)

#define UNREACHABLE __assume(0)

#elif defined(__GNUC__)

#define __debugbreak abort
#define UNREACHABLE __builtin_unreachable()
#define __cdecl

#endif
