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

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API NtDllExtension : public ExtensionLibrary
{
    friend class ExtensionLibrary;

public:
    NtDllExtension()
        : ExtensionLibrary(L"ntdll", L"ntdll.dll", L"ntdll.dll") {};
    virtual ~NtDllExtension() {}
    STDMETHOD(Initialize)();

    template <typename... Args>
    auto NtOpenFile(Args&&... args)
    {
        return NtCall(m_NtOpenFile, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtOpenDirectoryObject(Args&&... args)
    {
        return NtCall(m_NtOpenDirectoryObject, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtQueryInformationFile(Args&&... args)
    {
        return NtCall(m_NtQueryInformationFile, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtQueryInformationProcess(Args&&... args)
    {
        return NtCall(m_NtQueryInformationProcess, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtQueryDirectoryFile(Args&&... args)
    {
        return NtCall(m_NtQueryDirectoryFile, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtQuerySystemInformation(Args&&... args)
    {
        return NtCall(m_NtQuerySystemInformation, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtQueryObject(Args&&... args)
    {
        return NtCall(m_NtQueryObject, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtQueryDirectoryObject(Args&&... args)
    {
        return NtCall(m_NtQueryDirectoryObject, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtOpenSymbolicLinkObject(Args&&... args)
    {
        return NtCall(m_NtOpenSymbolicLinkObject, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto NtQuerySymbolicLinkObject(Args&&... args)
    {
        return NtCall(m_NtQuerySymbolicLinkObject, std::forward<Args>(args)...);
    }

    HRESULT RtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString)
    {
        DestinationString->Buffer = (PWSTR)SourceString;
        DestinationString->MaximumLength = DestinationString->Length = (USHORT)wcslen(SourceString) * sizeof(WCHAR);
        return S_OK;
    }

private:
    NTSTATUS(NTAPI* m_NtOpenFile)
    (PHANDLE phFile,
     ACCESS_MASK mask,
     POBJECT_ATTRIBUTES attributes,
     PIO_STATUS_BLOCK pio,
     ULONG ShareAccess,
     ULONG OpenOptions) = nullptr;
    NTSTATUS(NTAPI* m_NtOpenDirectoryObject)
    (_Out_ PHANDLE DirectoryHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes) = nullptr;
    NTSTATUS(NTAPI* m_NtQueryInformationFile)
    (HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS) = nullptr;
    NTSTATUS(NTAPI* m_NtQueryDirectoryFile)
    (HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, DWORD, BOOLEAN, PUNICODE_STRING, BOOLEAN) =
        nullptr;
    NTSTATUS(NTAPI* m_NtQueryInformationProcess)
    (IN HANDLE ProcessHandle,
     IN PROCESSINFOCLASS ProcessInformationClass,
     OUT PVOID ProcessInformation,
     IN ULONG ProcessInformationLength,
     OUT PULONG ReturnLength OPTIONAL) = nullptr;
    NTSTATUS(NTAPI* m_NtQuerySystemInformation)
    (__in SYSTEM_INFORMATION_CLASS SystemInformationClass,
     __inout PVOID SystemInformation,
     __in ULONG SystemInformationLength,
     __out_opt PULONG ReturnLength) = nullptr;
    NTSTATUS(NTAPI* m_NtQueryObject)
    (__in_opt HANDLE Handle,
     __in OBJECT_INFORMATION_CLASS ObjectInformationClass,
     __out_opt PVOID ObjectInformation,
     __in ULONG ObjectInformationLength,
     __out_opt PULONG ReturnLength) = nullptr;
    NTSTATUS(NTAPI* m_NtQueryDirectoryObject)
    (_In_ HANDLE DirectoryHandle,
     _Out_opt_ PVOID Buffer,
     _In_ ULONG Length,
     _In_ BOOLEAN ReturnSingleEntry,
     _In_ BOOLEAN RestartScan,
     _Inout_ PULONG Context,
     _Out_opt_ PULONG ReturnLength) = nullptr;
    NTSTATUS(NTAPI* m_NtOpenSymbolicLinkObject)
    (_Out_ PHANDLE LinkHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes) = nullptr;
    NTSTATUS(NTAPI* m_NtQuerySymbolicLinkObject)
    (_In_ HANDLE LinkHandle, _Inout_ PUNICODE_STRING LinkTarget, _Out_opt_ PULONG ReturnedLength) = nullptr;
};

}  // namespace Orc

#pragma managed(pop)
