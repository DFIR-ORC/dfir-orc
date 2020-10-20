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

class ORCLIB_API WMI
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

    stx::Result<CComPtr<IEnumWbemClassObject>, HRESULT> Query(LPCWSTR szQuery) const;

    HRESULT WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives) const;

    template <typename _T>
    static stx::Result<_T, HRESULT> GetProperty(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
    {
        static_assert(false, "Property read for your type must be via a specialised version");
    }

    ~WMI();
};

template <>
stx::Result<bool, HRESULT> Orc::WMI::GetProperty<bool>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);
template <>
stx::Result<USHORT, HRESULT> Orc::WMI::GetProperty<USHORT>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);
template <>
stx::Result<SHORT, HRESULT> Orc::WMI::GetProperty<SHORT>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
stx::Result<ULONG32, HRESULT> Orc::WMI::GetProperty<ULONG32>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
stx::Result<LONG32, HRESULT> Orc::WMI::GetProperty<LONG32>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
stx::Result<ULONG64, HRESULT> Orc::WMI::GetProperty<ULONG64>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);
template <>
stx::Result<LONG64, HRESULT> Orc::WMI::GetProperty<LONG64>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
stx::Result<ByteBuffer, HRESULT>
Orc::WMI::GetProperty<ByteBuffer>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
stx::Result<std::wstring, HRESULT>
Orc::WMI::GetProperty<std::wstring>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
stx::Result<std::vector<std::wstring>, HRESULT>
Orc::WMI::GetProperty<std::vector<std::wstring>>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

}  // namespace Orc

#pragma managed(pop)
