#pragma once

#include "types.h"

#define inrange(n,a,b) ((Uint)((n)-(a))<=(Uint)((b)-(a)))
#define inrangex(n,a,b) ((Uint)((n)-(a))<(Uint)((b)-(a)))

template <size_t size>
char (*__strlength_helper(char const (&_String)[size]))[size];
#define strlength(_String) (sizeof(*__strlength_helper(_String))-1)

#define STRING_WITH_LEN(s) s, strlength(s)

#ifndef _countof
#define _countof(array) (sizeof(array)/sizeof(array[0]))
#endif
