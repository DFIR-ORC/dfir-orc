//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

// Including SDKDDKVer.h defines the highest available Windows platform.

// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.

#ifndef WINVER  // Specifies that the minimum required platform is Windows Vista.
#    define WINVER 0x0520  // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINNT  // Specifies that the minimum required platform is Windows Vista.
#    define _WIN32_WINNT 0x0520  // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINDOWS  // Specifies that the minimum required platform is Windows 98.
#    define _WIN32_WINDOWS 0x0520  // Change this to the appropriate value to target Windows Me or later.
#endif
