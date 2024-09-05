//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers

#include <boost/version.hpp>
#if defined(_MSC_VER) && BOOST_VERSION == 105700
//#pragma warning(disable:4003)
#    define BOOST_PP_VARIADICS 0
#endif

#pragma warning(push)
#pragma warning(disable : 4996)

#include <inttypes.h>
#include <string>
#include <cstdio>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iterator>

#pragma warning(pop)

// Windows Header Files
#include <intrin.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <winnt.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>

#include <atlbase.h>
#include <strsafe.h>
#include <safeint.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <aclapi.h>
#include <Sddl.h>
#include <time.h>
#include <Fci.h>
#include <fcntl.h>
#include <Shlwapi.h>
#include <shlobj.h>
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma warning(disable : 4091)

#include <WinIoCtl.h>

#include <eh.h>

#include "Log/Log.h"
#include "Utils/Result.h"
