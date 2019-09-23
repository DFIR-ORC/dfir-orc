//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
#ifndef WINVER  // Specifies that the minimum required platform is Windows Vista.
#    define WINVER 0x0520  // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINNT  // Specifies that the minimum required platform is Windows Vista.
#    define _WIN32_WINNT 0x0520  // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINDOWS  // Specifies that the minimum required platform is Windows 98.
#    define _WIN32_WINDOWS 0x0520  // Change this to the appropriate value to target Windows Me or later.
#endif
