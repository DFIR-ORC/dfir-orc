//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "COMExtension.h"

using namespace Orc;

std::pair<HRESULT, HINSTANCE> Orc::COMExtension::LoadThisLibrary(const std::wstring& strLibFile)
{
    auto hInst = CoLoadLibrary((LPOLESTR)strLibFile.c_str(), FALSE);
    if (hInst == NULL)
    {
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
            return std::make_pair(hr, hInst);
        else
        {
            spdlog::debug(L"LoadLibraryEx failed without setting GetLastError()");
            return std::make_pair(hr, hInst);
        }
    }
    else
        return std::make_pair(S_OK, hInst);
}

STDMETHODIMP COMExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (!IsLoaded())
    {
        if (auto hr = Load(); FAILED(hr))
        {
            spdlog::error(L"Failed to load COM Extension {} (code: {:#x})", m_strKeyword, hr);
            return hr;
        }
    }
    Get(m_DllGetClassObject, "DllGetClassObject");
    m_bInitialized = true;
    return S_OK;
}

HRESULT COMExtension::AddClassFactory(CLSID clsid)
{
    HRESULT hr = E_FAIL;
    if (!IsInitialized())
        return E_FAIL;

    auto it = std::find_if(begin(m_ClassFactories), end(m_ClassFactories), [&clsid](const auto& item) {
        if (clsid == item.first)
            return true;
        return false;
    });

    if (it == end(m_ClassFactories))
    {
        CComPtr<IClassFactory> factory;

        if (FAILED(hr = m_DllGetClassObject(clsid, IID_IUnknown, (LPVOID*)&factory)))
        {
            return hr;
        }

        m_ClassFactories.push_back(std::make_pair(clsid, std::move(factory)));
    }
    return S_OK;
}

COMExtension::~COMExtension() {}

const CComPtr<IClassFactory>& COMExtension::GetClassFactory(CLSID clsid)
{
    static const CComPtr<IClassFactory> nullfactory;

    if (!IsInitialized())
    {
        if (auto hr = Initialize(); FAILED(hr))
        {
            return nullfactory;
        }
    }

    auto it = std::find_if(begin(m_ClassFactories), end(m_ClassFactories), [&clsid](const auto& item) {
        if (clsid == item.first)
            return true;
        return false;
    });

    if (it == end(m_ClassFactories))
        return nullfactory;
    else
        return it->second;
}
