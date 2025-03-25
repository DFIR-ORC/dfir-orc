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

#pragma warning(disable : 4251)  // disable pragma for template instantiations without Dll interface (and used in a
                                 // class with Dll interface)

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers

#pragma warning(push)
#pragma warning(disable : 4996)

#include <boost/version.hpp>
#if defined(_MSC_VER) && BOOST_VERSION == 105700
//#pragma warning(disable:4003)
#    define BOOST_PP_VARIADICS 0
#endif

#include <string>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
#include <memory>
#include <chrono>

#pragma warning(pop)

// Windows Header Files
#include <intrin.h>
#include <windows.h>
#include <atlbase.h>
#include <winnt.h>
#include <winternl.h>
#include <strsafe.h>
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
#include <concrt.h>
#include <ppl.h>

#include <WinIoCtl.h>

#include <eh.h>

#include "OrcLib.h"
