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

#include "LogFileWriter.h"

#include "Temporary.h"

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
        if (FAILED(
                hr = EmbeddedResource::ExtractToFile(
                    _L_, strRef, strKeyword, RESSOURCE_READ_EXECUTE_BA, m_strTempDirectory, strExtracted)))
        {
            log::Error(_L_, hr, L"Failed to extract ressource %s\r\n", strRef.c_str());
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
        log::Verbose(_L_, L"Ressource %s not found in temporary extracted ressources\r\n", strRef.c_str());
        return S_OK;  // Nothing to delete here
    }

    if (!it->second.empty())
    {
        log::Verbose(_L_, L"No temporary file associated with %s\r\n", strRef.c_str());
        return S_OK;
    }

    HRESULT hr = E_FAIL;
    if (FAILED(hr = UtilDeleteTemporaryFile(_L_, it->second.c_str())))
    {
        log::Error(
            _L_, hr, L"Failed to delete temporary ressource %s (temp is %s)\r\n", strRef.c_str(), it->second.c_str());
        return hr;
    }
    return S_OK;
}

HRESULT CommandAgentResources::DeleteTemporaryRessources()
{
    std::for_each(
        begin(m_TempRessources), end(m_TempRessources), [this](const std::pair<std::wstring, std::wstring>& item) {
            HRESULT hr = E_FAIL;

            if (FAILED(hr = UtilDeleteTemporaryFile(_L_, item.second.c_str())))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to delete temporary ressource %s (temp is %s)\r\n",
                    item.first.c_str(),
                    item.second.c_str());
            }
        });

    return S_OK;
}

CommandAgentResources::~CommandAgentResources()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = DeleteTemporaryRessources()))
    {
        log::Error(_L_, hr, L"Failed to delete all extrated temporary ressources (in ~CommandAgentResources)\r\n");
    }
}
