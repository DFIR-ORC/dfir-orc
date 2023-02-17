//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Utils/Result.h"
#include "Buffer.h"

#include <atlcomcli.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma managed(push, off)

namespace Orc {

class WMI
{

private:
    CComPtr<IWbemLocator> m_pLocator;
    CComPtr<IWbemServices> m_pServices;

public:
    HRESULT Initialize();

    HRESULT WMICreateProcess(
        LPCWSTR szCurrentDirectory,
        LPCWSTR szCommandLine,
        DWORD dwCreationFlags,
        DWORD dwPriority,
        DWORD& dwStatus);

    Result<CComPtr<IEnumWbemClassObject>> Query(LPCWSTR szQuery) const;

    HRESULT WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives) const;

    template <typename _T>
    static Result<_T> GetProperty(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
    {
        static_assert(!std::is_same_v<_T, _T>, "Property read for your type must be via a specialised version");
    }

    ~WMI();
};

template <>
Result<bool> Orc::WMI::GetProperty<bool>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<USHORT> Orc::WMI::GetProperty<USHORT>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<SHORT> Orc::WMI::GetProperty<SHORT>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<ULONG32> Orc::WMI::GetProperty<ULONG32>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<LONG32> Orc::WMI::GetProperty<LONG32>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<ULONG64> Orc::WMI::GetProperty<ULONG64>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<LONG64> Orc::WMI::GetProperty<LONG64>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<ByteBuffer> Orc::WMI::GetProperty<ByteBuffer>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<std::wstring> Orc::WMI::GetProperty<std::wstring>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<std::vector<std::wstring>>
Orc::WMI::GetProperty<std::vector<std::wstring>>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

}  // namespace Orc

#pragma managed(pop)
