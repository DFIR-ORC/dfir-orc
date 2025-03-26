//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
// FastFindCmd.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "Command/FastFind/FastFind.h"

using namespace std;

int wmain(int argc, const WCHAR* argv[])
{
    concurrency::Scheduler::SetDefaultSchedulerPolicy(concurrency::SchedulerPolicy(1, concurrency::MaxConcurrency, 16));

    return Orc::Command::UtilitiesMain::WMain<Orc::Command::FastFind::Main>(argc, argv);
}
