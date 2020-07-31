//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "CppUnitTest.h"

#include "LogFileWriter.h"

#include "EmbeddedResource.h"

#include "Robustness.h"

#include "UnitTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

BEGIN_TEST_MODULE_ATTRIBUTE()
TEST_MODULE_ATTRIBUTE(L"Date", L"2019/03/04")
END_TEST_MODULE_ATTRIBUTE()

TEST_MODULE_INITIALIZE(ModuleInitialize)
{
    Logger::WriteMessage(L"In Module Initialize");
    Orc::LogFileWriter L;
    L.SetLogCallback([](const WCHAR* szMsg, DWORD dwSize, DWORD& dwWritten) -> HRESULT {
        Logger::WriteMessage(szMsg);
        return S_OK;
    });
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    Logger::WriteMessage(L"In Module Cleanup");
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
