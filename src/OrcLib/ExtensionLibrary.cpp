//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ExtensionLibrary.h"
#include "EmbeddedResource.h"
#include "ParameterCheck.h"
#include "Temporary.h"

#include "SystemDetails.h"

#include "Robustness.h"

#include "WideAnsi.h"

#include <boost/algorithm/string.hpp>

#include <concrt.h>
#include <filesystem>

#include <spdlog/spdlog.h>

using namespace std;
using namespace Orc;

namespace Orc {

}

ExtensionLibrary::ExtensionLibrary(
    const std::wstring& strKeyword,
    const std::wstring& strX86LibRef,
    const std::wstring& strX64LibRef,
    const std::wstring& strTempDir)
    : m_strKeyword(strKeyword)
    , m_strX86LibRef(strX86LibRef)
    , m_strX64LibRef(strX64LibRef)
    , m_strTempDir(strTempDir)
{
}

bool Orc::ExtensionLibrary::CheckInitialized()
{
    HRESULT hr = E_FAIL;
    if (!IsInitialized())
    {
        if (FAILED(hr = Initialize()))
        {
            spdlog::error("Failed to initialize library (code: {:#x})", hr);
            return false;
        }
    }
    return true;
}

bool Orc::ExtensionLibrary::CheckLoaded()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = Load()))
    {
        spdlog::error(L"Failed to load library (code: {:#x})", hr);
        return false;
    }
    return true;
}

HRESULT ExtensionLibrary::TryLoad(const std::wstring& strFileRef)
{
    HRESULT hr = E_FAIL;

    std::wstring strSID;
    if (FAILED(hr = SystemDetails::UserSID(strSID)))
    {
        spdlog::error(L"Failed to get current user SID (code: {:#x})", hr);
        return hr;
    }

    if (EmbeddedResource::IsResourceBased(strFileRef))
    {
        std::wstring strExtractedFile;
        if (FAILED(
                hr = EmbeddedResource::ExtractToFile(
                    strFileRef,
                    m_strKeyword,
                    RESSOURCE_READ_EXECUTE_SID,
                    strSID.c_str(),
                    m_strTempDir,
                    strExtractedFile)))
        {
            spdlog::debug(
                L"Failed to extract resource '{}' into temp dir '{}' (code: {:#x})", strFileRef, m_strTempDir, hr);
            return hr;
        }
        m_bDeleteOnClose = true;

        if (FAILED(hr = ToDesiredName(strExtractedFile)))
            return hr;

        auto [hr, hModule] = LoadThisLibrary(m_strLibFile);
        if (hModule == NULL || FAILED(hr))
        {
            spdlog::debug(L"Failed to load extension lib using '{}' path (code: {:#x})", m_strLibFile, hr);
            return hr;
        }
        m_hModule = hModule;
        spdlog::debug(L"ExtensionLibrary: Loaded '{}' successfully", m_strLibFile);
        return S_OK;
    }

    spdlog::debug(L"ExtensionLibrary: Loading value '{}'", strFileRef);
    wstring strNewLibRef;
    if (SUCCEEDED(EmbeddedResource::ExtractValue(L"", strFileRef, strNewLibRef)))
    {
        spdlog::debug(L"ExtensionLibrary: Loaded value {}={} successfully", strFileRef, strNewLibRef);
        if (EmbeddedResource::IsResourceBased(strNewLibRef))
        {
            std::wstring strExtractedFile;

            if (FAILED(
                    hr = EmbeddedResource::ExtractToFile(
                        strNewLibRef,
                        m_strKeyword,
                        RESSOURCE_READ_EXECUTE_SID,
                        strSID.c_str(),
                        m_strTempDir,
                        strExtractedFile)))
            {
                spdlog::debug(
                    L"Failed to extract resource '{}' into temp dir '{}' (code: {:#x})",
                    strNewLibRef,
                    m_strTempDir,
                    hr);
                return hr;
            }
            m_bDeleteOnClose = true;

            if (FAILED(hr = ToDesiredName(strExtractedFile)))
                return hr;

            auto [hr, hModule] = LoadThisLibrary(m_strLibFile);
            if (hModule == NULL || FAILED(hr))
            {
                spdlog::debug(L"Failed to load extension lib using '{}' path (code: {:#x})", m_strLibFile, hr);
                return hr;
            }
            m_hModule = hModule;
            spdlog::debug(L"ExtensionLibrary: Loaded '{}' successfully", m_strLibFile);
            return S_OK;
        }

        // Last chance, try loading the file
        auto [hr, hModule] = LoadThisLibrary(strNewLibRef);
        if (hModule == NULL || FAILED(hr))
        {
            spdlog::debug(L"Failed to load extension lib using '{}' path (code: {:#x})", strNewLibRef, hr);
            return hr;
        }
        else
        {
            m_hModule = hModule;
            spdlog::debug(L"ExtensionLibrary: Loaded '{}' successfully", strNewLibRef);
        }
        m_bDeleteOnClose = false;

        WCHAR szFullPath[MAX_PATH];
        if (!GetModuleFileName(m_hModule, szFullPath, MAX_PATH))
        {
            spdlog::debug(L"Failed to get file path for extension lib '{}' (code: {:#x})", strNewLibRef, hr);
            return HRESULT_FROM_WIN32(GetLastError());
        }
        m_strLibFile = szFullPath;
        spdlog::debug(L"ExtensionLibrary: Loaded '{}' successfully", strNewLibRef);
        return S_OK;
    }

    // Last chance, try loading the file
    {
        auto [hr, hModule] = LoadThisLibrary(strFileRef);
        if (hModule == NULL || FAILED(hr))
        {
            spdlog::debug(L"Failed to load extension lib using '{}' path (code: {:#x})", strFileRef, hr);
            return hr;
        }
        else
        {
            m_hModule = hModule;
            spdlog::debug(L"ExtensionLibrary: Loaded '{}' successfully", strFileRef);
        }
        m_bDeleteOnClose = false;
    }
    WCHAR szFullPath[MAX_PATH];
    if (!GetModuleFileName(m_hModule, szFullPath, MAX_PATH))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::debug(L"Failed to get file path for extension lib '{}' (code: {:#x})", strFileRef, hr);
        return hr;
    }
    m_strLibFile = szFullPath;
    spdlog::debug(L"ExtensionLibrary: Loaded '{}' successfully", m_strLibFile);

    return S_OK;
}

std::pair<HRESULT, HINSTANCE> Orc::ExtensionLibrary::LoadThisLibrary(const std::wstring& strLibFile)
{
    auto hInst = LoadLibraryEx(strLibFile.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
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

HRESULT Orc::ExtensionLibrary::ToDesiredName(const std::wstring& libName)
{
    if (m_strDesiredName)
    {
        using namespace std::filesystem;

        path extractedFileName(libName);
        path finalName = extractedFileName.parent_path() / m_strDesiredName.value();

        std::error_code ec;
        std::filesystem::rename(extractedFileName, finalName, ec);

        if (ec)
        {
            spdlog::error(L"Failed to rename file '{}' to '{}': {})", extractedFileName, finalName, ec);
            return E_FAIL;
        }
        m_strLibFile = finalName.wstring();
    }
    else
        m_strLibFile = libName;
    return S_OK;
}

FARPROC Orc::ExtensionLibrary::GetEntryPoint(const CHAR* szFunctionName, bool bMandatory)
{
    HRESULT hr = E_FAIL;

    if (!IsLoaded() && FAILED(hr = Load()))
    {
        spdlog::error(L"Failed to load extension library '{}'", m_strKeyword);
        return nullptr;
    }

    FARPROC retval = GetProcAddress(m_hModule, szFunctionName);
    if (retval == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());

        if (bMandatory)
        {
            spdlog::error("Failed GetProcAddress on '{}' (code: {:#x})", szFunctionName, hr);
        }
        else
        {
            spdlog::debug("Failed GetProcAddress on '{}' (code: {:#x})", szFunctionName, hr);
        }
        return nullptr;
    }
    return retval;
}

HRESULT ExtensionLibrary::Load(const std::wstring& strAlternateRef)
{
    HRESULT hr = E_FAIL;

    if (m_hModule != NULL)
        return S_OK;

    if (!strAlternateRef.empty())
    {
        m_strLibRef = strAlternateRef;
    }
    else
    {
        WORD wArch = 0;
        if (FAILED(hr = SystemDetails::GetArchitecture(wArch)))
            return hr;

        switch (wArch)
        {
            case PROCESSOR_ARCHITECTURE_INTEL:
                m_strLibRef = m_strX86LibRef;
                break;
            case PROCESSOR_ARCHITECTURE_AMD64:
                if (SystemDetails::IsWOW64())
                    m_strLibRef = m_strX86LibRef;
                else
                    m_strLibRef = m_strX64LibRef;
                break;
            default:
                spdlog::error(L"Unsupported architecture: {}", wArch);
                return hr;
        }
    }

    std::vector<std::wstring> refs;

    spdlog::debug(L"TryLoad for references '{}'", m_strLibRef);

    boost::split(refs, m_strLibRef, boost::is_any_of(L";,"));

    for (auto& ref : refs)
    {
        if (SUCCEEDED(hr = TryLoad(ref)))
        {
            spdlog::debug(L"TryLoad succeeded for reference '{}'", ref);
            return S_OK;
        }
        else
        {
            spdlog::debug(L"TryLoad failed for reference '{}' (code: {:#x})", ref, hr);
        }
    }
    spdlog::error(L"Failed to load extension DLL '{}' using the reference values: '{}'", m_strKeyword, m_strLibRef);
    return hr;
}

STDMETHODIMP ExtensionLibrary::UnLoad()
{
    if (m_UnLoadHandler)
    {
        Robustness::RemoveTerminationHandler(m_UnLoadHandler);
        m_UnLoadHandler = nullptr;
    }

    if (m_hModule != NULL)
    {

        ScopedLock lock(m_cs);
        HMODULE hmod = m_hModule;
        m_hModule = NULL;
        FreeThisLibrary(hmod);
    }
    return S_OK;
}

STDMETHODIMP ExtensionLibrary::Cleanup()
{
    UnLoad();
    if (!m_strLibFile.empty() && m_bDeleteOnClose)
    {
        ScopedLock lock(m_cs);
        HRESULT hr = E_FAIL;

        if (FAILED(hr = UtilDeleteTemporaryFile(m_strLibFile.c_str())))
        {
            return hr;
        }
    }

    return S_OK;
}

STDMETHODIMP Orc::ExtensionLibrary::UnloadAndCleanup()
{
    if (m_UnLoadHandler)
    {
        Robustness::RemoveTerminationHandler(m_UnLoadHandler);
        m_UnLoadHandler = nullptr;
    }

    if (m_hModule != NULL)
    {
        ScopedLock lock(m_cs);
        HMODULE hmod = m_hModule;
        m_hModule = NULL;

        FreeThisLibrary(hmod);

        if (!m_strLibFile.empty() && m_bDeleteOnClose)
        {
            DWORD dwRetries = 0L;
            while (dwRetries < Orc::DELETION_RETRIES)
            {
                HRESULT hr = E_FAIL;
                if (SUCCEEDED(hr = UtilDeleteTemporaryFile(m_strLibFile.c_str(), 1)))
                {
                    return S_OK;
                }
                dwRetries++;
                FreeThisLibrary(hmod);
            }
        }
    }
    return S_OK;
}

ExtensionLibrary::~ExtensionLibrary(void)
{
    UnloadAndCleanup();
}
