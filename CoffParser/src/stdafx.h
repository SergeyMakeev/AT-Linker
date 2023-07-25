// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define _CRT_SECURE_NO_WARNINGS

#pragma warning ( disable : 4267 4018 )
//#include <iostream>
#include <tchar.h>
#include <stdlib.h>
#include <windows.h>

// TODO: reference additional headers your program requires here
#undef ASSERT
#ifdef _DEBUG
#  define ASSERT( a ) if ( !(a) ) __debugbreak();
#else
#  define ASSERT( a ) ((void)0)
#endif

#include <memory.h>
#include <vector>
#include <list>
#include <string>
#include <hash_map>
#include <algorithm>
#include <string.h>
using namespace std;
