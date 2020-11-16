//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "targetver.h"

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

// Do not declare fmt ostream/printf before any fmt specialization.
// Could be a regression from https://github.com/fmtlib/fmt/issues/952
#include "Output/Text/Fmt/Formatter.h"
#include <fmt/ostream.h>
#include <fmt/printf.h>
