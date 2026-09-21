//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Utils/Result.h"
#include "Buffer.h"

#include "Utils/Guard/Com.h"

#include <Wbemidl.h>

#pragma managed(push, off)

namespace Orc {

class WMI
{

private:
    Guard::ComPtr<IWbemLocator> m_pLocator;
    Guard::ComPtr<IWbemServices> m_pServices;

public:
    HRESULT Initialize();

    HRESULT WMICreateProcess(
        LPCWSTR szCurrentDirectory,
        LPCWSTR szCommandLine,
        DWORD dwCreationFlags,
        DWORD dwPriority,
        DWORD& dwStatus);

    Result<Guard::ComPtr<IEnumWbemClassObject>> Query(LPCWSTR szQuery) const;

    HRESULT WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives) const;

    template <typename _T>
    static Result<_T> GetProperty(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
    {
        static_assert(!std::is_same_v<_T, _T>, "Property read for your type must be via a specialised version");
    }

    ~WMI();
};

template <>
Result<bool> Orc::WMI::GetProperty<bool>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<USHORT> Orc::WMI::GetProperty<USHORT>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<SHORT> Orc::WMI::GetProperty<SHORT>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<ULONG32> Orc::WMI::GetProperty<ULONG32>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<LONG32> Orc::WMI::GetProperty<LONG32>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<ULONG64> Orc::WMI::GetProperty<ULONG64>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<LONG64> Orc::WMI::GetProperty<LONG64>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<ByteBuffer> Orc::WMI::GetProperty<ByteBuffer>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<std::wstring>
Orc::WMI::GetProperty<std::wstring>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

template <>
Result<std::vector<std::wstring>>
Orc::WMI::GetProperty<std::vector<std::wstring>>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty);

}  // namespace Orc

#pragma managed(pop)
