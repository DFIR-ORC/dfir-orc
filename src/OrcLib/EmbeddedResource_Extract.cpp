//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <safeint.h>
#include <agents.h>
#include <Psapi.h>

#include <filesystem>
#include <boost/scope_exit.hpp>

#include "EmbeddedResource.h"

#include "ResourceStream.h"
#include "FileStream.h"
#include "MemoryStream.h"
#include "ArchiveExtract.h"

#include "RunningProcesses.h"
#include "ParameterCheck.h"
#include "SystemDetails.h"
#include "Temporary.h"

#include "Kernel32Extension.h"

#include "CaseInsensitive.h"

#include "Log/Log.h"

#ifndef PROCESSOR_ARCHITECTURE_ARM64
#    define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;

constexpr const int REGEX_ARCHIVE_RESSOURCEREF = 1;
constexpr const int REGEX_ARCHIVE_FORMATREF = 2;
constexpr const int REGEX_ARCHIVE_MOTHERSHIP = 3;
constexpr const int REGEX_ARCHIVE_RESSOURCENAME = 4;
constexpr const int REGEX_ARCHIVE_FILENAME = 5;

constexpr auto REGEX_RES_RESSOURCEREF = 1;
constexpr auto REGEX_RES_MOTHERSHIP = 2;
constexpr auto REGEX_RES_RESSOURCENAME = 3;

constexpr auto REGEX_SELF_ARGUMENT = 1;

const WCHAR g_VALUES[] = L"VALUES";
const WCHAR g_BINARY[] = L"BINARY";

HINSTANCE g_DefaultResourceInstance = NULL;

EmbeddedResource::EmbeddedResource(void) {}

wregex& Orc::EmbeddedResource::ArchResourceRegEx()
{
    static wregex g_ArchResourceRegEx(
        L"((7z|zip):([a-zA-Z0-9_:\\-\\.\\/\\\\]*))#([a-zA-Z0-9_\\-\\.]+)\\|([a-zA-Z0-9\\-_\\*\\.\\/\\\\]+)");
    return g_ArchResourceRegEx;
}

std::wregex& Orc::EmbeddedResource::ResResourceRegEx()
{
    static wregex g_ResResourceRegEx(L"(res:([a-zA-Z0-9_:\\-\\.\\/\\\\]*))#([a-zA-Z0-9\\-_\\.\\/\\\\]+)");
    return g_ResResourceRegEx;
}

std::wregex& Orc::EmbeddedResource::SelfReferenceRegEx()
{
    static wregex g_SelfReferenceRegEx(L"self:#([a-zA-Z0-9]*)");
    return g_SelfReferenceRegEx;
}

void Orc::EmbeddedResource::SetDefaultHINSTANCE(HINSTANCE hInstance)
{
    g_DefaultResourceInstance = hInstance;
}

HINSTANCE Orc::EmbeddedResource::GetDefaultHINSTANCE()
{
    return g_DefaultResourceInstance;
}

const WCHAR* EmbeddedResource::VALUES()
{
    return g_VALUES;
}
const WCHAR* EmbeddedResource::BINARY()
{
    return g_BINARY;
}

bool EmbeddedResource::IsResourceBasedArchiveFile(const WCHAR* szCabFileName)
{
    return regex_match(szCabFileName, ArchResourceRegEx());
}
bool EmbeddedResource::IsResourceBased(const std::wstring& szImageFileResourceID)
{
    return regex_match(szImageFileResourceID.c_str(), ArchResourceRegEx())
        || regex_match(szImageFileResourceID.c_str(), ResResourceRegEx())
        || regex_match(szImageFileResourceID.c_str(), SelfReferenceRegEx());
}

bool EmbeddedResource::IsSelf(const std::wstring& szImageFileResourceID)
{
    return regex_match(szImageFileResourceID, SelfReferenceRegEx());
}

HRESULT EmbeddedResource::GetSelfArgument(const std::wstring& strRef, std::wstring& strArg)
{
    wsmatch s_self;
    if (regex_match(strRef, s_self, SelfReferenceRegEx()))
    {
        strArg = s_self[REGEX_SELF_ARGUMENT];
    }
    return S_OK;
}

HRESULT EmbeddedResource::SplitResourceReference(
    const std::wstring& Ref,
    std::wstring& HostBinary,
    std::wstring& ResName,
    std::wstring& NameInArchive,
    std::wstring& FormatName)
{
    wsmatch s_archive;

    if (!regex_match(Ref, s_archive, ArchResourceRegEx()))
    {
        Log::Debug(L"'{}' is not an embedded archive resource pattern", Ref);
        wsmatch s_res;
        if (!regex_match(Ref, s_res, ResResourceRegEx()))
        {
            wsmatch s_self;
            if (!regex_match(Ref, s_self, SelfReferenceRegEx()))
            {
                Log::Debug(L"'{}' is resource pattern for self reference", Ref);
            }
            else
            {
                Log::Debug(L"'{}' does not match a typical embedded resource pattern", Ref);
                return HRESULT_FROM_WIN32(ERROR_NO_MATCH);
            }
        }
        else
        {
            FormatName.assign(L"res");
            HostBinary = s_res[REGEX_RES_MOTHERSHIP];
            ResName = s_res[REGEX_RES_RESSOURCENAME];
            Log::Debug(L"'{}' is resource pattern for binary '{}' resource '{}'", Ref, HostBinary, ResName);
        }
    }
    else
    {
        // Resource is based in an archive...
        FormatName = s_archive[REGEX_ARCHIVE_FORMATREF];
        ResName = s_archive[REGEX_ARCHIVE_RESSOURCENAME];
        NameInArchive = s_archive[REGEX_ARCHIVE_FILENAME];
        HostBinary = s_archive[REGEX_ARCHIVE_MOTHERSHIP];
        Log::Debug(
            L"'{}' is an embedded archive resource pattern (binary: {}, res: {}, file: {}, format: {})",
            Ref,
            HostBinary,
            ResName,
            NameInArchive,
            FormatName);
    }
    return S_OK;
}

HRESULT EmbeddedResource::LocateResource(
    const std::wstring& HostBinary,
    const std::wstring& ResName,
    const WCHAR* szResType,
    HMODULE& hModule,
    HRSRC& hRes,
    std::wstring& strBinaryPath)
{
    HRESULT hr = E_FAIL;

    auto [dwMajor, dwMinor] = SystemDetails::GetOSVersion();
    DBG_UNREFERENCED_LOCAL_VARIABLE(dwMinor);
    DWORD dwLoadLibraryFlags = (dwMajor >= 6)
        ? LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_WITH_ALTERED_SEARCH_PATH
        : LOAD_LIBRARY_AS_DATAFILE | LOAD_WITH_ALTERED_SEARCH_PATH;

    HMODULE hInstance = NULL;

    if (HostBinary.empty())
        hInstance = GetDefaultHINSTANCE();
    else
    {
        hInstance = LoadLibraryEx(HostBinary.c_str(), NULL, dwLoadLibraryFlags);
        if (hInstance == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Warn(L"Resource file '{}' failed to load [{}], trying default instance", HostBinary, SystemError(hr));
            hInstance = GetDefaultHINSTANCE();
        }
    }

    HRSRC hResource = NULL;
    hRes = NULL;
    hModule = NULL;

    // First find and load the required resource
    if ((hResource = FindResource(hInstance, ResName.c_str(), szResType)) == NULL)
    {
        // checking parent process for "mothershipness"
        RunningProcesses rp;

        if (FAILED(hr = rp.EnumerateProcesses()))
            return hr;

        const DWORD dwGenerations = 2;
        DWORD dwParents[dwGenerations];
        dwParents[0] = rp.GetProcessParentID();

        for (UINT i = 1; i < dwGenerations; i++)
            dwParents[i] = rp.GetProcessParentID(dwParents[i - 1]);

        for (UINT i = 0; i < dwGenerations; i++)
        {
            if (dwParents[i])
            {
                HANDLE hParent = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwParents[i]);
                if (hParent == NULL)
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Debug(L"Failed to open process '{}' [{}]", dwParents[i], SystemError(hr));
                    continue;
                }

                BOOST_SCOPE_EXIT(&hParent)
                {
                    if (hParent)
                    {
                        CloseHandle(hParent);
                        hParent = NULL;
                    }
                }
                BOOST_SCOPE_EXIT_END;

                WCHAR szParentFileName[ORC_MAX_PATH];
                ZeroMemory(szParentFileName, ORC_MAX_PATH * sizeof(WCHAR));
                if (!GetModuleFileNameEx(hParent, NULL, szParentFileName, ORC_MAX_PATH))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Debug(L"Failed to get module file name for processID: {} [{}]", dwParents[i], SystemError(hr));
                    continue;
                }

                Log::Debug(L"Opening '{}' for resource '{}' of type '{}'", szParentFileName, ResName, szResType);

                hInstance = LoadLibraryEx(szParentFileName, NULL, dwLoadLibraryFlags);
                if (hInstance == NULL)
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Debug(L"Resource file '{}' failed to load [{}]", szParentFileName, SystemError(hr));
                    continue;
                }
                if ((hResource = FindResource(hInstance, ResName.c_str(), szResType)) == NULL)
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    FreeLibrary(hInstance);
                    continue;
                }

                hRes = hResource;
                hModule = hInstance;
                strBinaryPath.assign(szParentFileName);
                return S_OK;
            }
        }
        if (FAILED(hr))
            return hr;
    }
    else
    {
        WCHAR szFileName[ORC_MAX_PATH];
        ZeroMemory(szFileName, ORC_MAX_PATH * sizeof(WCHAR));
        if (!GetModuleFileName(hInstance, szFileName, ORC_MAX_PATH))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to get module file name for current process [{}]", SystemError(hr));
        }
        strBinaryPath.assign(szFileName);
        hRes = hResource;
        hModule = hInstance;
        return S_OK;
    }

    return S_OK;
}

bool EmbeddedResource::IsConfiguredToRun()
{
    HRESULT hr = S_OK;
    std::wstring strToExecuteRef;

    WORD Arch = 0;
    if (FAILED(hr = SystemDetails::GetArchitecture(Arch)))
    {
        return false;
    }

    switch (Arch)
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            if (FAILED(hr = EmbeddedResource::ExtractRun32(L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(L"", strToExecuteRef)))
                {
                    return false;
                }
            }
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            if (FAILED(hr = EmbeddedResource::ExtractRun64(L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(L"", strToExecuteRef)))
                {
                    return false;
                }
            }
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            if (FAILED(hr = EmbeddedResource::ExtractRunARM64(L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(L"", strToExecuteRef)))
                {
                    return false;
                }
            }
        default:
            return false;
    }

    return true;
}

HRESULT EmbeddedResource::ExtractRunWithArgs(
    const std::wstring& Module,
    std::wstring& strToExecuteRef,
    std::wstring& strRunArgs)
{
    DBG_UNREFERENCED_PARAMETER(Module);
    HRESULT hr = S_OK;

    WORD Arch = 0;
    if (FAILED(hr = SystemDetails::GetArchitecture(Arch)))
    {
        Log::Error("Failed to determine system architecture [{}]", SystemError(hr));
        return hr;
    }

    switch (Arch)
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            if (FAILED(hr = EmbeddedResource::ExtractRun32(L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(L"", strToExecuteRef)))
                {
                    Log::Error("Not RUN resource found to execute [{}]", SystemError(hr));
                    return hr;
                }
                else
                    EmbeddedResource::ExtractRunArgs(L"", strRunArgs);
            }
            else
                EmbeddedResource::ExtractRun32Args(L"", strRunArgs);
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            if (FAILED(hr = EmbeddedResource::ExtractRun64(L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(L"", strToExecuteRef)))
                {
                    Log::Error(L"Not RUN resource found to execute [{}]", SystemError(hr));
                    return hr;
                }
                else
                    EmbeddedResource::ExtractRunArgs(L"", strRunArgs);
            }
            else
                EmbeddedResource::ExtractRun64Args(L"", strRunArgs);
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            if (FAILED(hr = EmbeddedResource::ExtractRunARM64(L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(L"", strToExecuteRef)))
                {
                    Log::Error(L"Not RUN resource found to execute [{}]", SystemError(hr));
                    return hr;
                }
                else
                    EmbeddedResource::ExtractRunArgs(L"", strRunArgs);
            }
            else
                EmbeddedResource::ExtractRunARM64Args(L"", strRunArgs);
        default:
            Log::Error("Architecture '{}' is not supported", Arch);
            return E_FAIL;
    }

    return S_OK;
}

HRESULT EmbeddedResource::GetSelf(std::wstring& outputFile)
{
    WCHAR szModuleName[ORC_MAX_PATH];
    if (!GetModuleFileName(NULL, szModuleName, ORC_MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    outputFile.assign(szModuleName);
    return S_OK;
}

HRESULT Orc::EmbeddedResource::ExtractToFile(
    const std::wstring& szImageFileResourceID,
    const std::wstring& Keyword,
    LPCWSTR szSDDLFormat,
    LPCWSTR szSID,
    const std::wstring& strTempDir,
    std::wstring& outputFile)
{
    WCHAR szSDDL[ORC_MAX_PATH] = {0};

    swprintf_s(szSDDL, ORC_MAX_PATH, szSDDLFormat, szSID);

    return ExtractToFile(szImageFileResourceID, Keyword, szSDDL, strTempDir, outputFile);
}

HRESULT EmbeddedResource::ExtractToFile(
    const std::wstring& szImageFileResourceID,
    const std::wstring& Keyword,
    LPCWSTR szSDDL,
    const std::wstring& strOutputDir,
    std::wstring& outputFile)
{
    std::vector<std::pair<std::wstring, std::wstring>> outputFiles;
    if (auto hr = ExtractToDirectory(szImageFileResourceID, Keyword, szSDDL, strOutputDir, outputFiles); FAILED(hr))
        return hr;
    if (outputFiles.size() == 1)
    {
        std::swap(outputFile, outputFiles[0].second);
        return S_OK;
    }

    if (outputFiles.size() > 1)
    {
        Log::Error(L"More than one file were extracted from {}", szImageFileResourceID);
        return E_INVALIDARG;
    }

    Log::Error(L"No file extracted from {}", szImageFileResourceID);
    return E_INVALIDARG;
}

HRESULT Orc::EmbeddedResource::ExtractToDirectory(
    const std::wstring& szImageFileResourceID,
    const std::wstring& Keyword,
    LPCWSTR szSDDLFormat,
    LPCWSTR szSID,
    const std::wstring& strTempDir,
    std::vector<std::pair<std::wstring, std::wstring>>& outputFiles)
{
    WCHAR szSDDL[ORC_MAX_PATH] = {0};

    swprintf_s(szSDDL, ORC_MAX_PATH, szSDDLFormat, szSID);

    return ExtractToDirectory(szImageFileResourceID, Keyword, szSDDL, strTempDir, outputFiles);
}

HRESULT EmbeddedResource::ExtractToDirectory(
    const std::wstring& szImageFileResourceID,
    const std::wstring& Keyword,
    LPCWSTR szSDDL,
    const std::wstring& strOutputDir,
    std::vector<std::pair<std::wstring, std::wstring>>& outputFiles)
{
    WCHAR szTempDir[ORC_MAX_PATH];

    if (!strOutputDir.empty())
    {
        wcsncpy_s(szTempDir, strOutputDir.c_str(), strOutputDir.size());
    }
    else
    {
        if (auto hr = UtilGetTempDirPath(szTempDir, ORC_MAX_PATH); FAILED(hr))
            return hr;
    }

    if (IsSelf(szImageFileResourceID))
    {
        std::wstring myself;

        if (auto hr = GetSelf(myself); FAILED(hr))
            return hr;
        outputFiles.emplace_back(std::make_pair(myself, myself));
        return S_OK;
    }

    wstring MotherShip, ResName, NameInArchive, FormatName;

    if (auto hr = SplitResourceReference(szImageFileResourceID, MotherShip, ResName, NameInArchive, FormatName);
        FAILED(hr))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_NO_MATCH))
        {
            Log::Error(
                L"'{}' does not match a typical embedded resource pattern [{}]",
                szImageFileResourceID,
                SystemError(hr));
            return E_INVALIDARG;
        }
        else
        {
            Log::Error(
                L"'{}' failed to match a supported resource pattern [{}]", szImageFileResourceID, SystemError(hr));
            return E_INVALIDARG;
        }
    }

    if (NameInArchive.empty())
    {
        // Resource is directly embedded in resource

        shared_ptr<ResourceStream> res = make_shared<ResourceStream>();

        HRSRC hRes = NULL;
        HMODULE hModule = NULL;
        std::wstring strBinaryPath;
        if (auto hr = LocateResource(MotherShip, ResName, BINARY(), hModule, hRes, strBinaryPath); FAILED(hr))
        {
            Log::Warn(L"Could not locate resource '{}' [{}]", szImageFileResourceID, SystemError(hr));
            return hr;
        }

        if (auto hr = res->OpenForReadOnly(hModule, hRes); FAILED(hr))
            return hr;

        wstring fileName;
        HANDLE hFile = INVALID_HANDLE_VALUE;

        if (auto hr = UtilGetUniquePath(
                szTempDir,
                Keyword.c_str(),
                fileName,
                hFile,
                FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                szSDDL);
            FAILED(hr))
        {
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_EXISTS))
            {
                if (auto hr = UtilGetUniquePath(
                        szTempDir,
                        Keyword.c_str(),
                        fileName,
                        hFile,
                        FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                        szSDDL);
                    FAILED(hr))
                {
                    Log::Error(L"Failed to create extracted file '{}' [{}]", fileName, SystemError(hr));
                    return hr;
                }
            }
            else
            {
                Log::Error(L"Failed to create extracted file '{}' [{}]", fileName, SystemError(hr));
                return hr;
            }
        }

        shared_ptr<FileStream> fs = make_shared<FileStream>();
        if (auto hr = fs->OpenHandle(hFile); FAILED(hr))
        {
            Log::Error(L"Failed to create extracted '{}' [{}]", fileName, SystemError(hr));
            return hr;
        }
        ULONGLONG nbBytes = 0;
        if (auto hr = res->CopyTo(fs, &nbBytes); FAILED(hr))
        {
            Log::Error(L"Failed to copy resource to '{} [{}]'", fileName, SystemError(hr));
            return hr;
        }
        fs->Close();
        res->Close();

        outputFiles.emplace_back(std::make_pair(fileName, fileName));
    }
    else
    {
        // Resource is based in an archive... extracting file
        auto fmt = ArchiveExtract::GetArchiveFormat(FormatName);
        if (fmt == ArchiveFormat::Unknown)
            fmt = ArchiveFormat::SevenZip;

        auto extract = ArchiveExtract::MakeExtractor(fmt);

        vector<wstring> ToExtract;

        ToExtract.push_back(NameInArchive);
        if (auto hr = extract->Extract(szImageFileResourceID.c_str(), szTempDir, szSDDL, ToExtract); FAILED(hr))
            return hr;

        if (!extract->Items().empty())
        {
            for (const auto& item : extract->Items())
            {
                outputFiles.emplace_back(std::make_pair(item.NameInArchive, item.Path));
            }
        }
        else
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    return S_OK;
}

HRESULT
EmbeddedResource::ExtractToBuffer(const std::wstring& szImageFileResourceID, CBinaryBuffer& Buffer)
{
    HRESULT hr = E_FAIL;

    wstring MotherShip, ResName, NameInArchive, FormatName;

    if (SUCCEEDED(hr = SplitResourceReference(szImageFileResourceID, MotherShip, ResName, NameInArchive, FormatName)))
    {
        if (NameInArchive.empty())
        {
            // Resource is directly embedded in memory
            // Resource is directly embedded in resource

            shared_ptr<ResourceStream> res = make_shared<ResourceStream>();

            HRSRC hRes = NULL;
            HMODULE hModule = NULL;
            std::wstring strBinaryPath;
            if (FAILED(hr = LocateResource(MotherShip, ResName, BINARY(), hModule, hRes, strBinaryPath)))
            {
                Log::Debug(L"Failed to locate resource (id: {}) [{}]", szImageFileResourceID, SystemError(hr));
                return hr;
            }

            HGLOBAL hFileResource = NULL;
            // First find and load the required resource
            if ((hFileResource = LoadResource(hModule, hRes)) == NULL)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Debug(
                    L"Failed to load resource (id: {}, path: {}) [{}]",
                    szImageFileResourceID,
                    strBinaryPath,
                    SystemError(hr));
                return hr;
            }

            // Now open and map this to a disk file
            LPVOID lpData = NULL;
            if ((lpData = LockResource(hFileResource)) == NULL)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Debug(
                    L"Failed to lock resource (id: {}, path: {}) [{}]",
                    szImageFileResourceID,
                    strBinaryPath,
                    SystemError(hr));
                return hr;
            }

            DWORD dwSize = 0L;
            if ((dwSize = SizeofResource(hModule, hRes)) == 0)
            {
                HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Debug(
                    L"Failed to compute resource size (id: {}, path: {}) [{}]",
                    szImageFileResourceID,
                    strBinaryPath,
                    SystemError(hr));
                return hr;
            }

            if (auto hr = Buffer.SetData((LPBYTE)lpData, dwSize); FAILED(hr))
            {
                Log::Debug(
                    L"Failed to setup resource buffer (id: {}, path: {}) [{}]",
                    szImageFileResourceID,
                    strBinaryPath,
                    SystemError(hr));
                return hr;
            }
        }
        else
        {
            // Resource is based in an archive... extracting file
            auto fmt = ArchiveExtract::GetArchiveFormat(FormatName);
            if (fmt == ArchiveFormat::Unknown)
                fmt = ArchiveFormat::SevenZip;

            auto extract = ArchiveExtract::MakeExtractor(fmt);

            auto MakeArchiveStream =
                [&MotherShip, &ResName, &szImageFileResourceID](std::shared_ptr<ByteStream>& stream) -> HRESULT {
                HRESULT hr = E_FAIL;

                shared_ptr<ResourceStream> res = make_shared<ResourceStream>();

                HRSRC hRes = NULL;
                HMODULE hModule = NULL;
                std::wstring strBinaryPath;
                if (FAILED(hr = LocateResource(MotherShip, ResName, BINARY(), hModule, hRes, strBinaryPath)))
                {
                    Log::Debug(
                        L"Could not locate resource (id: {}, path: {}) [{}]",
                        szImageFileResourceID,
                        strBinaryPath,
                        SystemError(hr));
                    return hr;
                }

                if (FAILED(res->OpenForReadOnly(hModule, hRes)))
                    return hr;

                stream = res;
                return S_OK;
            };

            std::shared_ptr<MemoryStream> pOutput;
            auto MakeWriteStream = [&pOutput](OrcArchive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
                HRESULT hr = E_FAIL;

                auto stream = std::make_shared<MemoryStream>();

                if (item.Size > 0)
                {
                    using namespace msl::utilities;
                    if (FAILED(hr = stream->OpenForReadWrite(SafeInt<DWORD>(item.Size))))
                        return nullptr;
                    pOutput = stream;
                    return stream;
                }
                else
                {
                    if (FAILED(hr = stream->OpenForReadWrite()))
                        return nullptr;
                    pOutput = stream;
                    return stream;
                }
                return nullptr;
            };

            auto ShouldItemBeExtracted = [&NameInArchive](const std::wstring& strNameInArchive) -> bool {
                // Test if we wanna skip some files
                if (strNameInArchive == NameInArchive)
                    return true;
                return false;
            };
            if (FAILED(hr = extract->Extract(MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream)))
                return hr;

            if (!pOutput)
            {
                Log::Warn(L"Invalid output stream for item '{}' in resource '{}'", NameInArchive, ResName);
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            }
            pOutput->GrabBuffer(Buffer);
        }
    }
    else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MATCH))
    {
        Log::Error(
            L"'{}' does not match a typical embedded resource pattern [{}]", szImageFileResourceID, SystemError(hr));
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT EmbeddedResource::ExtractValue(const std::wstring& Module, const std::wstring& Name, std::wstring& Value)
{
    HRESULT hr = E_FAIL;
    HRSRC hRes = NULL;
    HMODULE hModule = NULL;
    std::wstring strBinaryPath;

    if (FAILED(hr = LocateResource(Module, Name, VALUES(), hModule, hRes, strBinaryPath)))
    {
        Log::Debug(L"Failed LocateResource '{}' in module '{}' [{}]", Name, Module, SystemError(hr));
        return hr;
    }
    else if (hModule == NULL && hRes == NULL)
    {
        Log::Debug(L"Failed to find '{}' in module '{}'", Name, Module);
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    }

    HGLOBAL hFileResource = LoadResource(hModule, hRes);
    if (hFileResource == NULL)
    {
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to LoadResource '{}' in module '{}' [{}]", Name, Module, SystemError(hr));
        return hr;
    }

    LPWSTR lpStr = NULL;
    if ((lpStr = (LPWSTR)LockResource(hFileResource)) == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to LockResource '{}' in module '{}' [{}]", Name, Module, SystemError(hr));
        if (hModule != NULL)
            FreeLibrary(hModule);
        return hr;
    }

    std::wstring ResourceSpec;
    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hModule, hRes)) == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to compute resource size '{}', in module '{}' [{}]", Name, Module, SystemError(hr));
        if (hModule != NULL)
            FreeLibrary(hModule);
        return hr;
    }
    ResourceSpec.reserve((dwSize / sizeof(WCHAR)) + 1);
    ResourceSpec.assign(lpStr, dwSize / sizeof(WCHAR));
    if (hModule != GetDefaultHINSTANCE())
        FreeLibrary(hModule);

    std::swap(ResourceSpec, Value);

    return S_OK;
}

HRESULT EmbeddedResource::ExtractBuffer(const std::wstring& Module, const std::wstring& Name, CBinaryBuffer& Value)
{
    HRESULT hr = E_FAIL;
    HRSRC hRes = NULL;
    std::wstring strBinaryPath;
    HMODULE hModule = NULL;

    if (FAILED(hr = LocateResource(Module, Name, BINARY(), hModule, hRes, strBinaryPath)))
    {
        return hr;
    }
    else if (hModule == NULL && hRes == NULL)
    {
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    }

    HGLOBAL hFileResource = LoadResource(hModule, hRes);
    if (hFileResource == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to LoadResource '{}' in module '{}' [{}]", Name, Module, SystemError(hr));
        return hr;
    }

    LPVOID lpVoid = NULL;
    if ((lpVoid = LockResource(hFileResource)) == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to LockResource '{}' in module '{}' [{}]", Name, Module, SystemError(hr));
        if (hModule != GetDefaultHINSTANCE())
            FreeLibrary(hModule);
        return hr;
    }

    CBinaryBuffer retval;
    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hModule, hRes)) == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to compute resource size '{}', in module '{}' [{}]", Name, Module, SystemError(hr));
        if (hModule != GetDefaultHINSTANCE())
            FreeLibrary(hModule);
        return hr;
    }

    retval.SetData((LPBYTE)lpVoid, dwSize);

    if (hModule != GetDefaultHINSTANCE())
        FreeLibrary(hModule);

    std::swap(retval, Value);

    return S_OK;
}

struct V_EnumResourceNamesEx_Param
{
    V_EnumResourceNamesEx_Param(const std::wstring& strModule, std::vector<EmbeddedResource::EmbedSpec>& values)
        : Module(strModule)
        , Values(values)
    {
    }

    const std::wstring& Module;
    std::vector<EmbeddedResource::EmbedSpec>& Values;
};

BOOL CALLBACK V_EnumResourceNamesEx_Function(HMODULE, LPCWSTR, LPWSTR lpszName, LONG_PTR lParam)
{
    HRESULT hr = E_FAIL;

    V_EnumResourceNamesEx_Param* param = (V_EnumResourceNamesEx_Param*)lParam;

    wstring strName(lpszName);
    wstring strValue;

    if (FAILED(hr = EmbeddedResource::ExtractValue(param->Module, strName, strValue)))
    {
        Log::Error(L"Failed to extract value '{}' [{}]", lpszName, SystemError(hr));
        return TRUE;
    }

    param->Values.emplace_back(EmbeddedResource::EmbedSpec::AddNameValuePair(std::move(strName), std::move(strValue)));
    return TRUE;
}

HRESULT EmbeddedResource::EnumValues(const std::wstring& Module, std::vector<EmbedSpec>& values)
{
    HRESULT hr = E_FAIL;
    HMODULE hInstance = NULL;

    DWORD dwMajor = 0, dwMinor = 0;
    if (FAILED(hr = SystemDetails::GetOSVersion(dwMajor, dwMinor)))
        return hr;
    DWORD dwLoadLibraryFlags = (dwMajor >= 6)
        ? LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_WITH_ALTERED_SEARCH_PATH
        : LOAD_LIBRARY_AS_DATAFILE | LOAD_WITH_ALTERED_SEARCH_PATH;

    // a host binary was specified, looking for that name, only in the same folder
    if (!Module.empty())
    {
        if (VerifyFileExists(Module.c_str()) == S_FALSE)
        {
            Log::Error(L"Resource file '{}' does not exist", Module);
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        hInstance = LoadLibraryEx(Module.c_str(), NULL, dwLoadLibraryFlags);
        if (hInstance == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError()),
            Log::Error(L"Resource file '{}' could not be loaded [{}]", Module, SystemError(hr));
            return hr;
        }
    }

    V_EnumResourceNamesEx_Param param = {Module, values};

    const auto pExt = ExtensionLibrary::GetLibrary<Kernel32Extension>();

    if (FAILED(hr = pExt->EnumResourceNamesW(hInstance, VALUES(), V_EnumResourceNamesEx_Function, (ULONG_PTR)&param)))
    {
        Log::Error(L"Failed to enumerate values resources [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

struct B_EnumResourceNamesEx_Param
{
    B_EnumResourceNamesEx_Param(const std::wstring& strModule, std::vector<EmbeddedResource::EmbedSpec>& values)
        : Module(strModule)
        , Values(values)
    {
    }
    const std::wstring& Module;
    std::vector<EmbeddedResource::EmbedSpec>& Values;
};

BOOL CALLBACK B_EnumResourceNamesEx_Function(HMODULE, LPCWSTR, LPWSTR lpszName, LONG_PTR lParam)
{

    HRESULT hr = E_FAIL;

    B_EnumResourceNamesEx_Param* param = (B_EnumResourceNamesEx_Param*)lParam;

    wstring strName(lpszName);
    CBinaryBuffer buffer;

    if (FAILED(hr = EmbeddedResource::ExtractBuffer(param->Module, strName, buffer)))
    {
        Log::Error(L"Failed to extract binary resource '{}' [{}]", lpszName, SystemError(hr));
        return TRUE;
    }
    param->Values.push_back(EmbeddedResource::EmbedSpec::AddBinary(std::move(strName), std::move(buffer)));

    return TRUE;
}

HRESULT EmbeddedResource::EnumBinaries(const std::wstring& Module, std::vector<EmbedSpec>& values)
{
    HRESULT hr = E_FAIL;
    HMODULE hInstance = NULL;

    DWORD dwMajor = 0, dwMinor = 0;
    if (FAILED(hr = SystemDetails::GetOSVersion(dwMajor, dwMinor)))
        return hr;
    DWORD dwLoadLibraryFlags = (dwMajor >= 6)
        ? LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_WITH_ALTERED_SEARCH_PATH
        : LOAD_LIBRARY_AS_DATAFILE | LOAD_WITH_ALTERED_SEARCH_PATH;

    // a host binary was specified, looking for that name, only in the same folder
    if (!Module.empty())
    {
        if (VerifyFileExists(Module.c_str()) == S_FALSE)
        {
            Log::Error(L"Resource file '{}' does not exist", Module);
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        hInstance = LoadLibraryEx(Module.c_str(), NULL, dwLoadLibraryFlags);
        if (hInstance == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error(L"Resource file '{}' could not be loaded [{}]", Module, SystemError(hr));
            return hr;
        }
    }

    B_EnumResourceNamesEx_Param param = {Module, values};

    const auto pExt = ExtensionLibrary::GetLibrary<Kernel32Extension>();

    if (FAILED(hr = pExt->EnumResourceNamesW(hInstance, BINARY(), B_EnumResourceNamesEx_Function, (ULONG_PTR)&param)))
    {
        Log::Error("Failed to enumerate values resources [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT EmbeddedResource::ExpandArchivesAndBinaries(const std::wstring& outDir, std::vector<EmbedSpec>& values)
{
    HRESULT hr = E_FAIL;

    std::vector<EmbedSpec> archives;
    std::vector<EmbedSpec> binaries;

    static const CHAR _7zSignature[] = "7z";
    static const size_t _7zSigLen = strlen(_7zSignature);

    for (auto& item : values)
    {

        std::wstring strArchFormat;

        if (item.Type == EmbedSpec::EmbedType::Buffer)
        {
            bool bArchive = true;

            if (item.BinaryValue.GetCount() > _7zSigLen)
            {
                for (size_t i = 0; i < _7zSigLen; i++)
                {
                    if (item.BinaryValue.Get<CHAR>(i) != _7zSignature[i])
                        bArchive = false;
                }
                if (bArchive)
                    strArchFormat = L"7z";
            }
            else
                bArchive = false;

            if (bArchive)
            {
                item.ArchiveFormat = strArchFormat;
                archives.push_back(item);
                item.Type = EmbedSpec::EmbedType::Void;
            }
            else
            {
                item.Type = EmbedSpec::EmbedType::File;
                binaries.push_back(item);
                item.Type = EmbedSpec::EmbedType::Void;
            }
        }
    }

    auto new_end = std::remove_if(
        begin(values), end(values), [](const EmbedSpec& item) { return item.Type == EmbedSpec::EmbedType::Void; });
    values.erase(new_end, end(values));

    for (const auto& item : binaries)
    {
        auto inBinary = std::make_shared<MemoryStream>();

        if (FAILED(hr = inBinary->OpenForReadOnly(item.BinaryValue.GetData(), item.BinaryValue.GetCount())))
        {
            Log::Error("Failed to open memory stream for resource [{}]", SystemError(hr));
            continue;
        }

        wstring out = outDir + L"\\" + item.Name;

        auto outFile = std::make_shared<FileStream>();

        if (FAILED(hr = outFile->WriteTo(out.c_str())))
        {
            Log::Error(L"Failed to create output file '{}' [{}]", out, SystemError(hr));
            continue;
        }
        ULONGLONG cbWritten = 0LL;

        if (FAILED(hr = inBinary->CopyTo(outFile, &cbWritten)))
        {
            Log::Error(L"Failed to write resource content to output file '{}' [{}]", out, SystemError(hr));
            continue;
        }
        values.push_back(EmbedSpec::AddFile(item.Name, out));
    }

    for (const auto& item : archives)
    {
        auto archStream = std::make_shared<MemoryStream>();

        if (FAILED(hr = archStream->OpenForReadOnly(item.BinaryValue.GetData(), item.BinaryValue.GetCount())))
        {
            Log::Error(L"Failed to open memory stream for resource [{}]", SystemError(hr));
            continue;
        }

        auto extractor = ArchiveExtract::MakeExtractor(OrcArchive::GetArchiveFormat(item.ArchiveFormat));
        if (!extractor)
        {
            Log::Error(L"Failed to create extractor for resource");
            continue;
        }

        wstring out = outDir + L"\\" + item.Name;

        std::error_code ec;
        fs::create_directories(fs::path(out), ec);
        if (ec)
        {
            Log::Error(L"Failed to create directory '{}' [{}]", out, ec);
            continue;
        }

        if (FAILED(hr = extractor->Extract(archStream, out.c_str(), nullptr)))
        {
            Log::Error(L"Failed to extract resource into '{}' [{}]", out, SystemError(hr));
            continue;
        }

        std::vector<EmbedSpec::ArchiveItem> archItems;

        for (const auto& extracted_item : extractor->Items())
        {
            EmbedSpec::ArchiveItem archItem;

            archItem.Name = extracted_item.NameInArchive;
            archItem.Path = extracted_item.Path;

            archItems.push_back(archItem);
        }

        values.push_back(EmbedSpec::AddArchive(item.Name, item.ArchiveFormat, item.ArchiveCompression, archItems));
    }

    return S_OK;
}

HRESULT EmbeddedResource::DeleteEmbeddedResources(
    const std::wstring& inModule,
    const std::wstring& outModule,
    std::vector<EmbedSpec>& values)
{
    HRESULT hr = E_FAIL;
    std::vector<EmbedSpec> to_delete;

    if (values.empty())
    {
        if (FAILED(hr = EnumValues(inModule, to_delete)))
        {
            Log::Error("Failed to enumerate resources to delete [{}]", SystemError(hr));
            return hr;
        }
        if (FAILED(hr = EnumBinaries(inModule, to_delete)))
        {
            Log::Error("Failed to enumerate resources to delete [{}]", SystemError(hr));
            return hr;
        }
    }
    else
    {
        to_delete = values;
    }

    for (auto& item : to_delete)
    {
        item.DeleteMe();
    }

    auto inStream = std::make_shared<FileStream>();
    std::wstring inputFile;

    if (inModule.empty())
    {
        if (FAILED(hr = GetSelf(inputFile)))
        {
            Log::Error(L"Failed to get my own path: '{}' [{}]", inputFile, SystemError(hr));
            return hr;
        }
    }
    else
    {
        inputFile = inModule;
    }

    if (FAILED(hr = inStream->ReadFrom(inputFile.c_str())))
    {
        Log::Error(L"Failed to open input binary '{}' to update resources [{}]", inputFile, SystemError(hr));
        return hr;
    }

    auto outStream = std::make_shared<FileStream>();
    if (FAILED(
            hr = outStream->OpenFile(
                outModule.c_str(), GENERIC_WRITE, 0L, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)))
    {
        Log::Error(L"Failed to open output binary '{}' to update resources [{}]", outModule, SystemError(hr));
        return hr;
    }
    ULONGLONG ullWritten = 0LL;
    ;

    if (FAILED(hr = inStream->CopyTo(outStream, &ullWritten)))
    {
        Log::Error(L"Failed to copy '{}' to '{}' [{}]", inputFile, outModule, SystemError(hr));
        return hr;
    }
    inStream->Close();
    outStream->Close();

    if (FAILED(hr = UpdateResources(outModule, to_delete)))
    {
        Log::Error(L"Failed to update resources in '{}' [{}]", outModule, SystemError(hr));
        return hr;
    }

    return S_OK;
}

EmbeddedResource::~EmbeddedResource(void) {}
