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
#include "LogFileWriter.h"
#include "Temporary.h"

#include "SystemDetails.h"

#include "Robustness.h"

#include <boost/algorithm/string.hpp>

#include <concrt.h>
#include <filesystem>

using namespace std;
using namespace Orc;

namespace Orc {

}

ExtensionLibrary::ExtensionLibrary(
    logger pLog,
    const std::wstring& strKeyword,
    const std::wstring& strX86LibRef,
    const std::wstring& strX64LibRef,
    const std::wstring& strTempDir)
    : _L_(std::move(pLog))
    , m_strKeyword(strKeyword)
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
            log::Error(_L_, hr, L"Failed to initialize library\r\n");
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
        log::Error(_L_, hr, L"Failed to load library\r\n");
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
        log::Error(_L_, hr, L"Failed to get current user SID\r\n");
        return hr;
    }

    if (EmbeddedResource::IsResourceBased(strFileRef))
    {
        std::wstring strExtractedFile;
        if (FAILED(
                hr = EmbeddedResource::ExtractToFile(
                    _L_,
                    strFileRef,
                    m_strKeyword,
                    RESSOURCE_READ_EXECUTE_SID,
                    strSID.c_str(),
                    m_strTempDir,
                    strExtractedFile)))
        {
            log::Verbose(
                _L_, L"Failed to extract resource %s into temp dir %s\r\n", strFileRef.c_str(), m_strTempDir.c_str());
            return hr;
        }
        m_bDeleteOnClose = true;

        if (FAILED(hr = ToDesiredName(strExtractedFile)))
            return hr;

        auto [hr, hModule] = LoadThisLibrary(m_strLibFile);
        if (hModule == NULL || FAILED(hr))
        {
            log::Verbose(_L_, L"Failed to load extension lib using %s path\r\n", m_strLibFile.c_str());
            return hr;
        }
        m_hModule = hModule;
        log::Verbose(_L_, L"ExtensionLibrary: Loaded %s successfully\r\n", m_strLibFile.c_str());
        return S_OK;
    }

    log::Verbose(_L_, L"ExtensionLibrary: Loading value %s\r\n", strFileRef.c_str());
    wstring strNewLibRef;
    if (SUCCEEDED(EmbeddedResource::ExtractValue(_L_, L"", strFileRef, strNewLibRef)))
    {
        log::Verbose(
            _L_, L"ExtensionLibrary: Loaded value %s=%s successfully\r\n", strFileRef.c_str(), strNewLibRef.c_str());
        if (EmbeddedResource::IsResourceBased(strNewLibRef))
        {
            std::wstring strExtractedFile;

            if (FAILED(
                    hr = EmbeddedResource::ExtractToFile(
                        _L_,
                        strNewLibRef,
                        m_strKeyword,
                        RESSOURCE_READ_EXECUTE_SID,
                        strSID.c_str(),
                        m_strTempDir,
                        strExtractedFile)))
            {
                log::Verbose(
                    _L_,
                    L"Failed to extract resource %s into temp dir %s\r\n",
                    strNewLibRef.c_str(),
                    m_strTempDir.c_str());
                return hr;
            }
            m_bDeleteOnClose = true;

            if (FAILED(hr = ToDesiredName(strExtractedFile)))
                return hr;

            auto [hr, hModule] = LoadThisLibrary(m_strLibFile);
            if (hModule == NULL || FAILED(hr))
            {
                log::Verbose(_L_, L"Failed to load extension lib using %s path\r\n", m_strLibFile.c_str());
                return hr;
            }
            m_hModule = hModule;
            log::Verbose(_L_, L"ExtensionLibrary: Loaded %s successfully\r\n", m_strLibFile.c_str());
            return S_OK;
        }

        // Last chance, try loading the file
        auto [hr, hModule] = LoadThisLibrary(strNewLibRef);
        if (hModule == NULL || FAILED(hr))
        {
            log::Verbose(_L_, L"Failed to load extension lib using %s path\r\n", strNewLibRef.c_str());
            return hr;
        }
        else
        {
            m_hModule = hModule;
            log::Verbose(_L_, L"ExtensionLibrary: Loaded %s successfully\r\n", strNewLibRef.c_str());
        }
        m_bDeleteOnClose = false;

        WCHAR szFullPath[MAX_PATH];
        if (!GetModuleFileName(m_hModule, szFullPath, MAX_PATH))
        {
            log::Verbose(_L_, L"Failed to get file path for extension lib %s\r\n", strNewLibRef.c_str());
            return HRESULT_FROM_WIN32(GetLastError());
        }
        m_strLibFile = szFullPath;
        log::Verbose(_L_, L"ExtensionLibrary: Loaded %s successfully\r\n", strNewLibRef.c_str());
        return S_OK;
    }

    // Last chance, try loading the file
    {
        auto [hr, hModule] = LoadThisLibrary(strFileRef);
        if (hModule == NULL || FAILED(hr))
        {
            log::Verbose(_L_, L"Failed to load extension lib using %s path\r\n", strFileRef.c_str());
            return hr;
        }
        else
        {
            m_hModule = hModule;
            log::Verbose(_L_, L"ExtensionLibrary: Loaded %s successfully\r\n", m_strLibFile.c_str());
        }
        m_bDeleteOnClose = false;
    }
    WCHAR szFullPath[MAX_PATH];
    if (!GetModuleFileName(m_hModule, szFullPath, MAX_PATH))
    {
        log::Verbose(_L_, L"Failed to get file path for extension lib %s\r\n", strFileRef.c_str());
        return HRESULT_FROM_WIN32(GetLastError());
    }
    m_strLibFile = szFullPath;
    log::Verbose(_L_, L"ExtensionLibrary: Loaded %s successfully\r\n", m_strLibFile.c_str());

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
            log::Verbose(_L_, L"LoadLibraryEx failed without setting GetLastError()");
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
            log::Error(_L_, E_FAIL, L"Failed to rename file %s to %s (%S)", extractedFileName.c_str(), finalName.c_str(), ec.message().c_str());
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
        log::Error(_L_, hr, L"Failed to load extension library %s\r\n", m_strKeyword.c_str());
        return nullptr;
    }

    FARPROC retval = GetProcAddress(m_hModule, szFunctionName);
    if (retval == NULL)
    {
        if (bMandatory)
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Could not resolve %S\r\n", szFunctionName);
        }
        else
        {
            log::Verbose(_L_, L"Could not resolve %S\r\n", szFunctionName);
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
                log::Error(_L_, hr, L"Unsupported architecture %d\r\n", wArch);
                return hr;
        }
    }

    std::vector<std::wstring> refs;

    log::Verbose(_L_, L"TryLoad for references %s\r\n", m_strLibRef.c_str());

    boost::split(refs, m_strLibRef, boost::is_any_of(L";,"));

    for (auto& ref : refs)
    {
        log::Verbose(_L_, L"TryLoad reference: %s\r\n", ref.c_str());
        if (SUCCEEDED(hr = TryLoad(ref)))
        {
            log::Verbose(_L_, L"TryLoad succeeded for reference %s\r\n", ref.c_str());
            return S_OK;
        }
        else
        {
            log::Verbose(_L_, L"TryLoad failed for reference %s 0x%lx\r\n", ref.c_str(), hr);
        }
    }
    log::Error(
        _L_,
        hr,
        L"Failed to load extension DLL %s using the reference values: %s\r\n",
        m_strKeyword.c_str(),
        m_strLibRef.c_str());
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
    _L_.reset();
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
