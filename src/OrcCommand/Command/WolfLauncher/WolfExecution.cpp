//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"
#include "WolfExecution.h"

using namespace Orc::Command::Wolf;

WolfExecution::~WolfExecution(void)
{
    TerminateAllAndComplete();
}
