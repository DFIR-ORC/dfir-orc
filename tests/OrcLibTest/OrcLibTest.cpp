//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "EmbeddedResource.h"

#include "Robustness.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

BEGIN_TEST_MODULE_ATTRIBUTE()
TEST_MODULE_ATTRIBUTE(L"Date", L"2016/01/05")
END_TEST_MODULE_ATTRIBUTE()

TEST_MODULE_INITIALIZE(ModuleInitialize)
{
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    Robustness::Terminate();
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    // Remove all managed code from here and put it in constructor of A.
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        Orc::EmbeddedResource::SetDefaultHINSTANCE(hInstance);
    }
    return true;
}
