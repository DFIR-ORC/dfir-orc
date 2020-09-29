//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ExtensionLibrary.h"

#include <windows.h>
#include <functional>
#include <winternl.h>
#include <psapi.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API PSAPIExtension : public ExtensionLibrary
{

public:
    PSAPIExtension()
        : ExtensionLibrary(L"psapi", L"psapi.dll", L"psapi.dll") {};

    STDMETHOD(Initialize)();

    HRESULT WINAPI EnumProcessModules(_In_ HANDLE hProcess, _Out_ HMODULE* lphModule, _In_ DWORD cb, LPDWORD lpcbNeeded)
    {

        if (FAILED(Initialize()))
            return E_FAIL;

        if (m_EnumProcessModulesEx != nullptr)
        {
            if (!m_EnumProcessModulesEx(hProcess, lphModule, cb, lpcbNeeded, LIST_MODULES_ALL))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            return S_OK;
        }

        if (m_EnumProcessModules != nullptr)
        {
            if (!m_EnumProcessModules(hProcess, lphModule, cb, lpcbNeeded))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            return S_OK;
        }
        return S_OK;
    }

private:
    BOOL(WINAPI* m_EnumProcessModulesEx)
    (_In_ HANDLE hProcess, _Out_ HMODULE* lphModule, _In_ DWORD cb, _Out_ LPDWORD lpcbNeeded, _In_ DWORD dwFilterFlag) =
        nullptr;
    BOOL(WINAPI* m_EnumProcessModules)
    (_In_ HANDLE hProcess, _Out_ HMODULE* lphModule, _In_ DWORD cb, _Out_ LPDWORD lpcbNeeded) = nullptr;
};
}  // namespace Orc
#pragma managed(pop)
