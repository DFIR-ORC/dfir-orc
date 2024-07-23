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

#include <string>

#pragma managed(push, off)

namespace Orc {

using namespace std::string_literals;

class Kernel32Extension : public ExtensionLibrary
{
    friend class ExtensionLibrary;

public:
    Kernel32Extension()
        : ExtensionLibrary(L"kernel32"s, L"kernel32.dll"s) {};

    template <typename... Args>
    auto InitializeProcThreadAttributeList(Args&&... args)
    {
        return Win32Call(m_InitializeProcThreadAttributeList, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto UpdateProcThreadAttribute(Args&&... args)
    {
        return Win32Call(m_UpdateProcThreadAttribute, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto DeleteProcThreadAttributeList(Args&&... args)
    {
        return VoidCall(m_DeleteProcThreadAttributeList, std::forward<Args>(args)...);
    }

    HRESULT CancelIoEx(_In_ HANDLE hFile, _In_opt_ LPOVERLAPPED lpOverlapped)
    {
        if (FAILED(Initialize()))
            return E_FAIL;
        if (m_CancelIoEx != nullptr)
            m_CancelIoEx(hFile, lpOverlapped);
        else
            CancelIo(hFile);
        return S_OK;
    }

    HRESULT EnumResourceNamesW(
        _In_opt_ HMODULE hModule,
        _In_ LPCWSTR lpszType,
        _In_ ENUMRESNAMEPROC lpEnumFunc,
        _In_ LONG_PTR lParam)
    {

        if (FAILED(Initialize()))
            return E_FAIL;

        if (m_EnumResourceNamesExW != nullptr)
        {
            if (!m_EnumResourceNamesExW(hModule, lpszType, lpEnumFunc, lParam, 0L, LANG_NEUTRAL))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        else if (m_EnumResourceNamesW != nullptr)
        {
            if (!m_EnumResourceNamesW(hModule, lpszType, lpEnumFunc, lParam))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        else
        {
            return E_FAIL;
        }
        return S_OK;
    };

    HANDLE ReOpenFile(_In_ HANDLE hOriginalFile, _In_ DWORD dwDesiredAccess, _In_ DWORD dwShareMode, _In_ DWORD dwFlags)
    {
        if (FAILED(Initialize()))
            return INVALID_HANDLE_VALUE;
        if (m_ReOpenFile == nullptr)
            return INVALID_HANDLE_VALUE;
        else
            return m_ReOpenFile(hOriginalFile, dwDesiredAccess, dwShareMode, dwFlags);
    }

    STDMETHOD(Initialize)();

private:
    BOOL(WINAPI* m_InitializeProcThreadAttributeList)
    (_Out_opt_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
     _In_ DWORD dwAttributeCount,
     _Reserved_ DWORD dwFlags,
     _Inout_ PSIZE_T lpSize) = nullptr;
    BOOL(WINAPI* m_UpdateProcThreadAttribute)
    (_Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
     _In_ DWORD dwFlags,
     _In_ DWORD_PTR Attribute,
     _In_ PVOID lpValue,
     _In_ SIZE_T cbSize,
     _Out_opt_ PVOID lpPreviousValue,
     _In_opt_ PSIZE_T lpReturnSize) = nullptr;
    VOID(WINAPI* m_DeleteProcThreadAttributeList)(_Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList) = nullptr;
    VOID(WINAPI* m_CancelIoEx)(_In_ HANDLE hFile, _In_opt_ LPOVERLAPPED lpOverlapped) = nullptr;

    BOOL(WINAPI* m_EnumResourceNamesExW)
    (_In_opt_ HMODULE hModule,
     _In_ LPCWSTR lpszType,
     _In_ ENUMRESNAMEPROC lpEnumFunc,
     _In_ LONG_PTR lParam,
     _In_ DWORD dwFlags,
     _In_ LANGID LangId) = nullptr;
    BOOL(WINAPI* m_EnumResourceNamesW)
    (_In_opt_ HMODULE hModule, _In_ LPCWSTR lpszType, _In_ ENUMRESNAMEPROC lpEnumFunc, _In_ LONG_PTR lParam) = nullptr;

    HANDLE(WINAPI* m_ReOpenFile)
    (_In_ HANDLE hOriginalFile, _In_ DWORD dwDesiredAccess, _In_ DWORD dwShareMode, _In_ DWORD dwFlags) = nullptr;
};

}  // namespace Orc

#pragma managed(pop)
