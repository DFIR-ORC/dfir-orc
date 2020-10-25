//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "CommandAgentResources.h"

#include "EmbeddedResource.h"

#include "Temporary.h"

#include "Log/Log.h"

using namespace std;
using namespace Orc;

HRESULT CommandAgentResources::ExtractRessource(
    const std::wstring& strRef,
    const std::wstring& strKeyword,
    std::wstring& strExtracted)
{
    HRESULT hr = E_FAIL;

    if (EmbeddedResource::IsResourceBased(strRef))
    {
        hr = EmbeddedResource::ExtractToFile(
            strRef, strKeyword, RESSOURCE_READ_EXECUTE_BA, m_strTempDirectory, strExtracted);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to extract ressource '{}' [{}]", strRef, SystemError(hr));
            return hr;
        }
    }
    return S_OK;
}

HRESULT CommandAgentResources::GetResource(
    const std::wstring& strRef,
    const std::wstring& strKeyword,
    std::wstring& strExtracted)
{
    HRESULT hr = E_FAIL;

    auto it = m_TempRessources.find(strRef);
    if (it == end(m_TempRessources))
    {
        if (FAILED(hr = ExtractRessource(strRef, strKeyword, strExtracted)))
        {
            // We failed to extract this ressource, save this information as an empty extracted path
            m_TempRessources[strRef] = L"";
            return hr;
        }
        else
        {
            m_TempRessources[strRef] = strExtracted;
            return S_OK;
        }
    }
    else
    {
        strExtracted = it->second;
        if (strExtracted.empty())
            return HRESULT_FROM_WIN32(ERROR_RESOURCE_NOT_FOUND);
        else
            return S_OK;
    }
}

HRESULT CommandAgentResources::DeleteTemporaryRessource(const std::wstring& strRef)
{
    auto it = m_TempRessources.find(strRef);

    if (it == end(m_TempRessources))
    {
        Log::Debug(L"Ressource '{}' not found in temporary extracted ressources", strRef);
        return S_OK;  // Nothing to delete here
    }

    if (!it->second.empty())
    {
        Log::Debug(L"No temporary file associated with '{}'", strRef);
        return S_OK;
    }

    HRESULT hr = E_FAIL;
    if (FAILED(hr = UtilDeleteTemporaryFile(it->second.c_str())))
    {
        Log::Error(
            L"Failed to delete temporary ressource '{}' (temp: '{}', [{}])", strRef, it->second, SystemError(hr));
        return hr;
    }
    return S_OK;
}

HRESULT CommandAgentResources::DeleteTemporaryRessources()
{
    std::for_each(
        begin(m_TempRessources), end(m_TempRessources), [this](const std::pair<std::wstring, std::wstring>& item) {
            HRESULT hr = E_FAIL;

            if (FAILED(hr = UtilDeleteTemporaryFile(item.second.c_str())))
            {
                Log::Error(
                    L"Failed to delete temporary ressource {} (temp: {}, [{}])",
                    item.first,
                    item.second,
                    SystemError(hr));
            }
        });

    return S_OK;
}

CommandAgentResources::~CommandAgentResources()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = DeleteTemporaryRessources()))
    {
        Log::Error(L"~CommandAgentResources: failed to delete all extrated temporary ressources [{}]", SystemError(hr));
    }
}
