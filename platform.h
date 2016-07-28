// Compiler shims

#if defined(_MSC_VER)

#elif defined(__GNUC__)

#define __debugbreak abort
#define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)
#define __cdecl

#endif
