//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OrcResult.h"
#include "Buffer.h"

#include <atlcomcli.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API WMI
{

private:
    logger _L_;
    CComPtr<IWbemLocator> m_pLocator;
    CComPtr<IWbemServices> m_pServices;

public:
    WMI(logger pLog)
        : _L_(std::move(pLog)) {};

    HRESULT Initialize();

    HRESULT WMICreateProcess(
        LPCWSTR szCurrentDirectory,
        LPCWSTR szCommandLine,
        DWORD dwCreationFlags,
        DWORD dwPriority,
        DWORD& dwStatus);

    Result<CComPtr<IEnumWbemClassObject>> Query(LPCWSTR szQuery);

    HRESULT WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives);

    template <typename _T>
    static Result<_T> GetProperty(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty)
    {
        static_assert(false, "Property read for your type must be via a specialised version");
    }


    ~WMI();
};

template <>
Orc::Result<USHORT> Orc::WMI::GetProperty<USHORT>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);
template <>
Orc::Result<SHORT> Orc::WMI::GetProperty<SHORT>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);

template <>
Orc::Result<ULONG32> Orc::WMI::GetProperty<ULONG32>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);

template <>
Orc::Result<LONG32> Orc::WMI::GetProperty<LONG32>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);

template <>
Orc::Result<ULONG64> Orc::WMI::GetProperty<ULONG64>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);
template <>
Orc::Result<LONG64> Orc::WMI::GetProperty<LONG64>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);

template <>
Orc::Result<ByteBuffer> Orc::WMI::GetProperty<ByteBuffer>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);

template <>
Orc::Result<std::wstring>
Orc::WMI::GetProperty<std::wstring>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);

template <>
Orc::Result<std::vector<std::wstring>>
Orc::WMI::GetProperty<std::vector<std::wstring>>(const CComPtr<IWbemClassObject> obj, LPCWSTR szProperty);

}  // namespace Orc

#pragma managed(pop)
