//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "targetver.h"

// <winsock2.h> must come before <windows.h>: windows.h pulls the original <winsock.h>, and including
// <winsock2.h> after it redefines those functions with different linkage. The OrcLib headers below
// reach <winsock2.h>, so without this the precompiled header does not compile.
// WIN32_LEAN_AND_MEAN is NOT the fix here (it is what src/OrcLib/stdafx.h uses): it also drops the
// COM/OLE declarations the OrcLib stream headers need.
#include <winsock2.h>

// Headers for CppUnitTest
#include "CppUnitTest.h"

#include <windows.h>
#include <winioctl.h>

#include <string>
#include <cstdio>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iterator>

#include <atlbase.h>

#include "Log/Log.h"

#include "unittesthelper.h"
