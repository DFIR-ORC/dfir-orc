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

#include <spdlog/spdlog.h>

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
            spdlog::error(L"Failed to extract ressource '{}' (code: {:#x})", strRef, hr);
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
        spdlog::debug(L"Ressource '{}' not found in temporary extracted ressources", strRef);
        return S_OK;  // Nothing to delete here
    }

    if (!it->second.empty())
    {
        spdlog::debug(L"No temporary file associated with '{}'", strRef);
        return S_OK;
    }

    HRESULT hr = E_FAIL;
    if (FAILED(hr = UtilDeleteTemporaryFile(it->second.c_str())))
    {
        spdlog::error(L"Failed to delete temporary ressource '{}' (temp: '{}', code: {:#x})", strRef, it->second, hr);
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
                spdlog::error(
                    L"Failed to delete temporary ressource {} (temp: {}, code: {:#x})", item.first, item.second, hr);
            }
        });

    return S_OK;
}

CommandAgentResources::~CommandAgentResources()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = DeleteTemporaryRessources()))
    {
        spdlog::error(L"~CommandAgentResources: failed to delete all extrated temporary ressources (code: {:#x})", hr);
    }
}
