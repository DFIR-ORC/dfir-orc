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
#include "Text/Fmt/std_filesystem.h"
#include "Text/Fmt/std_optional.h"

#include "Log/Log.h"

#include "WideAnsi.h"

#include <boost/algorithm/string.hpp>

#include <concrt.h>
#include <filesystem>

using namespace std;
using namespace Orc;

namespace Orc {

}

ExtensionLibrary::ExtensionLibrary(
    const std::wstring& strKeyword,
    const std::wstring& strX86LibRef,
    const std::wstring& strX64LibRef,
    std::vector<std::shared_ptr<DependencyLibrary>> dependencies)
    : m_strKeyword(strKeyword)
    , m_strX86LibRef(strX86LibRef)
    , m_strX64LibRef(strX64LibRef)
    , m_Dependencies(std::move(dependencies))
{
}

HRESULT Orc::ExtensionLibrary::LoadDependencies(std::optional<std::filesystem::path> tempDir)
{
    for (const auto& dependency : m_Dependencies)
    {
        if (dependency->IsLoaded())
            continue;
        if (auto hr = dependency->Load(tempDir); FAILED(hr))
        {
            Log::Error(L"Failed to load dependency library '{}'", dependency->Keyword());
            return hr;
        }
        else
        {
            Log::Debug(L"Loaded dependency library '{}'", dependency->Keyword());
        }
    }
    return S_OK;
}

HRESULT ExtensionLibrary::TryLoad(const std::wstring& strFileRef)
{
    using namespace std::filesystem;

    std::wstring strSID;
    if (auto hr = SystemDetails::UserSID(strSID); FAILED(hr))
    {
        Log::Error(L"Failed to get current user SID [{}]", SystemError(hr));
        return hr;
    }

    if (EmbeddedResource::IsResourceBased(strFileRef))
    {
        std::wstring strExtractedFile;
        if (auto hr = EmbeddedResource::ExtractToFile(
                strFileRef,
                m_strKeyword,
                RESSOURCE_READ_EXECUTE_SID,
                strSID.c_str(),
                m_tempDir.wstring(),
                strExtractedFile);
            FAILED(hr))
        {
            Log::Debug(
                L"Failed to extract resource '{}' into temp dir '{}' [{}]",
                strFileRef,
                m_tempDir.wstring(),
                SystemError(hr));
            return hr;
        }
        m_bDeleteOnClose = true;

        if (auto hr = ToDesiredName(strExtractedFile); FAILED(hr))
        {
            Log::Warn(
                L"Failed to extract rename extracted file from '{}' to '{}' [{}]",
                strExtractedFile,
                m_strDesiredName,
                SystemError(hr));
            m_libFile = strExtractedFile;
        }

        auto [hr, hModule] = LoadThisLibrary(m_libFile);
        if (hModule == NULL || FAILED(hr))
        {
            Log::Debug(L"Failed to load extension lib using '{}' path [{}]", m_libFile, SystemError(hr));
            return hr;
        }
        m_hModule = hModule;
        Log::Debug(L"ExtensionLibrary: Loaded '{}' successfully", m_libFile);
        return S_OK;
    }

    Log::Debug(L"ExtensionLibrary: Loading value '{}'", strFileRef);
    wstring strNewLibRef;
    if (auto hr = EmbeddedResource::ExtractValue(L"", strFileRef, strNewLibRef); SUCCEEDED(hr))
    {
        Log::Debug(L"ExtensionLibrary: Loaded value {}={} successfully", strFileRef, strNewLibRef);
        if (EmbeddedResource::IsResourceBased(strNewLibRef))
        {
            std::vector<std::pair<std::wstring, std::wstring>> strExtractedFiles;

            if (FAILED(
                    hr = EmbeddedResource::ExtractToDirectory(
                        strNewLibRef,
                        m_strKeyword,
                        RESSOURCE_READ_EXECUTE_SID,
                        strSID.c_str(),
                        m_tempDir.wstring(),
                        strExtractedFiles)))
            {
                Log::Debug(
                    L"Failed to extract resource '{}' into temp dir '{}' [{}]",
                    strNewLibRef,
                    m_tempDir,
                    SystemError(hr));
                return hr;
            }

            for (const auto& file : strExtractedFiles)
            {
                path archive_path = file.first;
                path extracted_path = file.second;

                // When extracting extension libraries, we need the files to keep their original file names for proper
                // dependency loading
                path desired_path = extracted_path;
                desired_path.replace_filename(archive_path.filename());

                std::error_code ec;
                rename(extracted_path, desired_path, ec);
                if (ec)
                {
                    Log::Warn(
                        L"Failed to rename extension library name {} to {} [{}]", extracted_path, desired_path, ec);
                    m_libFile = extracted_path;
                }
                else if (m_strDesiredName)
                {
                    if (boost::iequals(desired_path.filename().c_str(), *m_strDesiredName))
                    {
                        m_libFile = desired_path;
                    }
                }
                else
                {
                    m_libFile = desired_path;
                }
            }
            if (m_libFile.empty())
            {
                Log::Error(L"Desired file name '{}' was not found in extracted files", m_strDesiredName);
                return E_INVALIDARG;
            }

            m_bDeleteOnClose = true;

            auto [hr, hModule] = LoadThisLibrary(m_libFile);
            if (hModule == NULL || FAILED(hr))
            {
                Log::Debug(L"Failed to load extension lib using '{}' path [{}]", m_libFile, SystemError(hr));
                return hr;
            }
            m_hModule = hModule;
            Log::Debug(L"ExtensionLibrary: Loaded '{}' successfully", m_libFile);
            return S_OK;
        }

        // Last chance, try loading the file
        if (auto [hr, hModule] = LoadThisLibrary(strNewLibRef); hModule == NULL || FAILED(hr))
        {
            Log::Debug(L"Failed to load extension lib using '{}' path [{}]", strNewLibRef, SystemError(hr));
            return hr;
        }
        else
        {
            m_hModule = hModule;
            Log::Debug(L"ExtensionLibrary: Loaded '{}' successfully", strNewLibRef);
        }
        m_bDeleteOnClose = false;

        WCHAR szFullPath[ORC_MAX_PATH];
        if (!GetModuleFileName(m_hModule, szFullPath, ORC_MAX_PATH))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed to get file path for extension lib '{}' [{}]", strNewLibRef, SystemError(hr));
            return hr;
        }
        m_libFile = szFullPath;
        Log::Debug(L"ExtensionLibrary: Loaded '{}' successfully", strNewLibRef);
        return S_OK;
    }

    // Last chance, try loading the file
    {
        auto [hr, hModule] = LoadThisLibrary(strFileRef);
        if (hModule == NULL || FAILED(hr))
        {
            Log::Debug(L"Failed to load extension lib using '{}' path [{}]", strFileRef, SystemError(hr));
            return hr;
        }
        else
        {
            m_hModule = hModule;
            Log::Debug(L"ExtensionLibrary: Loaded '{}' successfully", strFileRef);
        }
        m_bDeleteOnClose = false;
    }
    WCHAR szFullPath[ORC_MAX_PATH];
    if (!GetModuleFileName(m_hModule, szFullPath, ORC_MAX_PATH))
    {
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(L"Failed to get file path for extension lib '{}' [{}]", strFileRef, SystemError(hr));
        return hr;
    }
    m_libFile = szFullPath;
    Log::Debug(L"ExtensionLibrary: Loaded '{}' successfully", m_libFile);

    return S_OK;
}

std::pair<HRESULT, HINSTANCE> Orc::ExtensionLibrary::LoadThisLibrary(const std::filesystem::path& libFile)
{
    auto hInst = LoadLibraryEx(libFile.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (hInst == NULL)
    {
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
            return std::make_pair(hr, hInst);
        else
        {
            Log::Debug("LoadLibraryEx failed without setting GetLastError()");
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
            Log::Warn(L"Failed to rename file '{}' to '{}': {})", extractedFileName, finalName, ec);

        m_libFile = finalName;
    }
    else
        m_libFile = libName;
    return S_OK;
}

FARPROC Orc::ExtensionLibrary::GetEntryPoint(const CHAR* szFunctionName, bool bMandatory)
{
    HRESULT hr = E_FAIL;

    if (!IsLoaded() && FAILED(hr = Load()))
    {
        Log::Error(L"Failed to load extension library '{}'", m_strKeyword);
        return nullptr;
    }

    FARPROC retval = GetProcAddress(m_hModule, szFunctionName);
    if (retval == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());

        if (bMandatory)
        {
            Log::Error("Failed GetProcAddress on '{}' [{}]", szFunctionName, SystemError(hr));
        }
        else
        {
            Log::Debug("Failed GetProcAddress on '{}' [{}]", szFunctionName, SystemError(hr));
        }
        return nullptr;
    }
    return retval;
}

HRESULT ExtensionLibrary::Load(std::optional<std::filesystem::path> tempDir)
{
    using namespace std::filesystem;

    if (m_hModule != NULL)
        return S_OK;

    if (auto hr = LoadDependencies(tempDir); FAILED(hr))
    {
        Log::Error(L"Failed to load dependencies for extension library '{}'", m_strKeyword);
        return hr;
    }


    WORD wArch = 0;
    if (auto hr = SystemDetails::GetArchitecture(wArch); FAILED(hr))
        return hr;

    if (tempDir.has_value())
    {
        if (exists(*tempDir))
        {
            if (is_directory(*tempDir))
            {
                Log::Debug(L"Using existing directory {} to load extension library", tempDir);
                m_tempDir = std::move(*tempDir);
            }
            else
                Log::Warn(
                    L"Specified temporary directory {} for extenstion library exists and is not a directory -> ignored",
                    tempDir);
        }
        else
        {
            Log::Debug(L"Creating temporary directory {} for extension library", tempDir);
            std::error_code ec;
            if (!create_directories(*tempDir, ec))
            {
                Log::Error(
                    L"Specified temporary directory {} for extenstion library could not be created -> ignored",
                    tempDir);
            }
            else if (!ec)
            {
                m_tempDir = std::move(*tempDir);
                m_bDeleteTemp = true;
            }
        }
    }

    if (m_tempDir.empty())
    {
        m_tempDir = DefaultExtensionDirectory();
    }

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
            Log::Error(L"Unsupported architecture: {}", wArch);
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    std::vector<std::wstring> refs;

    Log::Debug(L"TryLoad for references '{}'", m_strLibRef);

    boost::split(refs, m_strLibRef, boost::is_any_of(L";,"));

    auto last_hr = E_FAIL;
    for (auto& ref : refs)
    {
        if (auto hr = TryLoad(ref); SUCCEEDED(hr))
        {
            Log::Debug(L"TryLoad succeeded for reference '{}'", ref);
            return S_OK;
        }
        else
        {
            Log::Debug(L"TryLoad failed for reference '{}' [{}]", ref, SystemError(hr));
            last_hr = hr;
        }
    }
    Log::Error(L"Failed to load extension DLL '{}' using the reference values: '{}'", m_strKeyword, m_strLibRef);
    return last_hr;
}

HRESULT ExtensionLibrary::UnLoad()
{
    // Remove the termination handler
    if (m_UnLoadHandler)
    {
        Robustness::RemoveTerminationHandler(m_UnLoadHandler);
        m_UnLoadHandler = nullptr;
    }

    // Unload the library
    if (m_hModule != NULL)
    {
        ScopedLock lock(m_cs);
        HMODULE hmod = m_hModule;
        m_hModule = NULL;
        FreeThisLibrary(hmod);
    }

    // Unload dependencies (at least decrement ref count)
    m_Dependencies.clear();
    return S_OK;
}

HRESULT ExtensionLibrary::Cleanup()
{
    UnLoad();
    if (!m_libFile.empty() && m_bDeleteOnClose)
    {
        ScopedLock lock(m_cs);

        if (auto hr = UtilDeleteTemporaryFile(m_libFile); FAILED(hr))
            return hr;
    }

    std::error_code ec;
    if (m_bDeleteTemp && !m_tempDir.empty() && std::filesystem::exists(m_tempDir, ec))
    {
        if (auto hr = UtilDeleteTemporaryDirectory(m_tempDir); FAILED(hr))
            return hr;
    }

    return S_OK;
}

HRESULT Orc::ExtensionLibrary::UnloadAndCleanup()
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

    if (!m_libFile.empty() && m_bDeleteOnClose)
    {
        DWORD dwRetries = 0L;
        while (dwRetries < Orc::DELETION_RETRIES)
        {
            HRESULT hr = E_FAIL;
            if (SUCCEEDED(hr = UtilDeleteTemporaryFile(m_libFile.c_str(), 1)))
            {
                return S_OK;
            }
            dwRetries++;

            if (m_hModule != NULL) // retry FreeLibrary
            {
                ScopedLock lock(m_cs);
                HMODULE hmod = m_hModule;
                m_hModule = NULL;

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

const std::filesystem::path&
Orc::ExtensionLibrary::DefaultExtensionDirectory(std::optional<std::filesystem::path> aDefaultDir)
{
    static std::filesystem::path defaultDir;

    if (aDefaultDir.has_value())
    {
        if (exists(*aDefaultDir))
        {
            if (is_directory(*aDefaultDir))
            {
                Log::Debug(L"Using existing directory {} as default to load extension library", aDefaultDir);
                defaultDir = std::move(*aDefaultDir);
            }
            else
                Log::Warn(
                    L"Specified temporary directory {} as default for extenstion library exists and is not a directory "
                    L"-> ignored",
                    aDefaultDir);
        }
        else
        {
            Log::Debug(L"Creating temporary directory {} as default for extension library", aDefaultDir);
            std::error_code ec;
            if (!create_directories(*aDefaultDir, ec))
            {
                Log::Error(
                    L"Specified temporary directory {} as default for extenstion library could not be created -> "
                    L"ignored",
                    aDefaultDir);
            }
            else if (!ec)
            {
                defaultDir = std::move(*aDefaultDir);
            }
        }
    }

    if (defaultDir.empty())
    {
        WCHAR szTempDir[ORC_MAX_PATH];
        if (auto hr = UtilGetTempDirPath(szTempDir, ORC_MAX_PATH); SUCCEEDED(hr))
            defaultDir = szTempDir;
    }

    return defaultDir;
}
