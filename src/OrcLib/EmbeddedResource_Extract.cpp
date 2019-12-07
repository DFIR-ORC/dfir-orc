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

#include "LogFileWriter.h"

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

HINSTANCE g_DefaultRessourceInstance = NULL;

EmbeddedResource::EmbeddedResource(void) {}

wregex& Orc::EmbeddedResource::ArchRessourceRegEx()
{
    static wregex g_ArchRessourceRegEx(
        L"((cab|7z|zip):([a-zA-Z0-9_\\-\\.]*))#([a-zA-Z0-9_\\-\\.]+)\\|([a-zA-Z0-9\\-_\\.]+)");
    return g_ArchRessourceRegEx;
}

std::wregex& Orc::EmbeddedResource::ResRessourceRegEx()
{
    static wregex g_ResRessourceRegEx(L"(res:([a-zA-Z0-9\\-_\\.]*))#([a-zA-Z0-9\\-_\\.]+)");
    return g_ResRessourceRegEx;
}

std::wregex& Orc::EmbeddedResource::SelfReferenceRegEx()
{
    static wregex g_SelfReferenceRegEx(L"self:#([a-zA-Z0-9]*)");
    return g_SelfReferenceRegEx;
}

void Orc::EmbeddedResource::SetDefaultHINSTANCE(HINSTANCE hInstance)
{
    g_DefaultRessourceInstance = hInstance;
}

HINSTANCE Orc::EmbeddedResource::GetDefaultHINSTANCE()
{
    return g_DefaultRessourceInstance;
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
    return regex_match(szCabFileName, ArchRessourceRegEx());
}
bool EmbeddedResource::IsResourceBased(const std::wstring& szImageFileRessourceID)
{
    return regex_match(szImageFileRessourceID.c_str(), ArchRessourceRegEx())
        || regex_match(szImageFileRessourceID.c_str(), ResRessourceRegEx())
        || regex_match(szImageFileRessourceID.c_str(), SelfReferenceRegEx());
}

bool EmbeddedResource::IsSelf(const std::wstring& szImageFileRessourceID)
{
    return regex_match(szImageFileRessourceID, SelfReferenceRegEx());
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
    const logger& pLog,
    const std::wstring& Ref,
    std::wstring& HostBinary,
    std::wstring& ResName,
    std::wstring& NameInArchive,
    std::wstring& FormatName)
{
    wsmatch s_archive;

    if (!regex_match(Ref, s_archive, ArchRessourceRegEx()))
    {
        log::Verbose(pLog, L"%s is not an embedded archive ressource pattern\r\n", Ref.c_str());
        wsmatch s_res;
        if (!regex_match(Ref, s_res, ResRessourceRegEx()))
        {
            wsmatch s_self;
            if (!regex_match(Ref, s_self, SelfReferenceRegEx()))
            {
                log::Verbose(pLog, L"%s is ressource pattern for self reference (%s)\r\n", Ref.c_str());
            }
            else
            {
                log::Verbose(pLog, L"\"%s\" does not match a typical embedded ressource pattern\r\n", Ref.c_str());
                return HRESULT_FROM_WIN32(ERROR_NO_MATCH);
            }
        }
        else
        {
            FormatName.assign(L"res");
            HostBinary = s_res[REGEX_RES_MOTHERSHIP];
            ResName = s_res[REGEX_RES_RESSOURCENAME];
            log::Verbose(
                pLog,
                L"%s is ressource pattern for binary %s resource %s\r\n",
                Ref.c_str(),
                HostBinary.c_str(),
                ResName.c_str());
        }
    }
    else
    {
        // Resource is based in an archive...
        FormatName = s_archive[REGEX_ARCHIVE_FORMATREF];
        ResName = s_archive[REGEX_ARCHIVE_RESSOURCENAME];
        NameInArchive = s_archive[REGEX_ARCHIVE_FILENAME];
        HostBinary = s_archive[REGEX_ARCHIVE_MOTHERSHIP];
        log::Verbose(
            pLog,
            L"%s is an embedded archive ressource pattern (binary=%s,res=%s,file=%s,format=%s)\r\n",
            Ref.c_str(),
            HostBinary.c_str(),
            ResName.c_str(),
            NameInArchive.c_str(),
            FormatName.c_str());
    }
    if (!HostBinary.empty())
    {
        log::Error(
            pLog,
            E_NOTIMPL,
            L"Host binary %s specified in ressource %s is no longer implemented\r\n",
            HostBinary.c_str(),
            Ref.c_str());
        return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT EmbeddedResource::LocateResource(
    const logger& pLog,
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
            log::Warning(
                pLog,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Ressource file %s failed to load, trying default instance\r\n",
                HostBinary.c_str());
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
        RunningProcesses rp(pLog);

        if (FAILED(hr = rp.EnumProcesses()))
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
                    log::Verbose(
                        pLog,
                        L"Failed to open process %d (hr=0x%lx)\r\n",
                        dwParents[i],
                        hr = HRESULT_FROM_WIN32(GetLastError()));
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

                WCHAR szParentFileName[MAX_PATH];
                ZeroMemory(szParentFileName, MAX_PATH * sizeof(WCHAR));
                if (!GetModuleFileNameEx(hParent, NULL, szParentFileName, MAX_PATH))
                {
                    log::Verbose(
                        pLog,
                        L"Failed to get module file name for processID:%d (hr=0x%lx)\r\n",
                        dwParents[i],
                        hr = HRESULT_FROM_WIN32(GetLastError()));
                    continue;
                }

                log::Verbose(
                    pLog,
                    L"INFO: Opening %s for ressource %s of type %s\r\n",
                    szParentFileName,
                    ResName.c_str(),
                    szResType);

                hInstance = LoadLibraryEx(szParentFileName, NULL, dwLoadLibraryFlags);
                if (hInstance == NULL)
                {
                    log::Verbose(
                        pLog,
                        L"Ressource file %s failed to load (hr=0x%lx)\r\n",
                        szParentFileName,
                        hr = HRESULT_FROM_WIN32(GetLastError()));
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
        WCHAR szFileName[MAX_PATH];
        ZeroMemory(szFileName, MAX_PATH * sizeof(WCHAR));
        if (!GetModuleFileName(hInstance, szFileName, MAX_PATH))
        {
            log::Verbose(
                pLog,
                L"Failed to get module file name for current process (hr=0x%lx)\r\n",
                hr = HRESULT_FROM_WIN32(GetLastError()));
        }
        strBinaryPath.assign(szFileName);
        hRes = hResource;
        hModule = hInstance;
        return S_OK;
    }

    return S_OK;
}

bool EmbeddedResource::IsConfiguredToRun(const logger& pLog)
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
            if (FAILED(hr = EmbeddedResource::ExtractRun32(pLog, L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(pLog, L"", strToExecuteRef)))
                {
                    return false;
                }
            }
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            if (FAILED(hr = EmbeddedResource::ExtractRun64(pLog, L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(pLog, L"", strToExecuteRef)))
                {
                    return false;
                }
            }
            break;
        default:
            return false;
    }

    return true;
}

HRESULT EmbeddedResource::ExtractRunWithArgs(
    const logger& pLog,
    const std::wstring& Module,
    std::wstring& strToExecuteRef,
    std::wstring& strRunArgs)
{
    DBG_UNREFERENCED_PARAMETER(Module);
    HRESULT hr = S_OK;

    WORD Arch = 0;
    if (FAILED(hr = SystemDetails::GetArchitecture(Arch)))
    {
        log::Error(pLog, hr, L"Failed to determine system architecture\r\n");
        return hr;
    }

    switch (Arch)
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            if (FAILED(hr = EmbeddedResource::ExtractRun32(pLog, L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(pLog, L"", strToExecuteRef)))
                {
                    log::Error(pLog, hr, L"Not RUN ressource found to execute\r\n");
                    return hr;
                }
                else
                    EmbeddedResource::ExtractRunArgs(pLog, L"", strRunArgs);
            }
            else
                EmbeddedResource::ExtractRun32Args(pLog, L"", strRunArgs);
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            if (FAILED(hr = EmbeddedResource::ExtractRun64(pLog, L"", strToExecuteRef)))
            {
                if (FAILED(hr = EmbeddedResource::ExtractRun(pLog, L"", strToExecuteRef)))
                {
                    log::Error(pLog, hr, L"Not RUN ressource found to execute\r\n");
                    return hr;
                }
                else
                    EmbeddedResource::ExtractRunArgs(pLog, L"", strRunArgs);
            }
            else
                EmbeddedResource::ExtractRun64Args(pLog, L"", strRunArgs);

            break;
        default:
            log::Error(pLog, E_FAIL, L"Architecture %d is not supported\r\n", Arch);
            return E_FAIL;
    }

    return S_OK;
}

HRESULT EmbeddedResource::GetSelf(std::wstring& outputFile)
{
    WCHAR szModuleName[MAX_PATH];
    if (!GetModuleFileName(NULL, szModuleName, MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    outputFile.assign(szModuleName);
    return S_OK;
}

HRESULT Orc::EmbeddedResource::ExtractToFile(
    const logger& pLog,
    const std::wstring& szImageFileRessourceID,
    const std::wstring& Keyword,
    LPCWSTR szSDDLFormat,
    LPCWSTR szSID,
    const std::wstring& strTempDir,
    std::wstring& outputFile)
{
    WCHAR szSDDL[MAX_PATH] = {0};

    swprintf_s(szSDDL, MAX_PATH, szSDDLFormat, szSID);

    return ExtractToFile(pLog, szImageFileRessourceID, Keyword, szSDDL, strTempDir, outputFile);
}

HRESULT EmbeddedResource::ExtractToFile(
    const logger& pLog,
    const std::wstring& szImageFileRessourceID,
    const std::wstring& Keyword,
    LPCWSTR szSDDL,
    const std::wstring& strOutputDir,
    std::wstring& outputFile)
{
    HRESULT hr = E_FAIL;

    WCHAR szTempDir[MAX_PATH];

    if (!strOutputDir.empty())
    {
        wcsncpy_s(szTempDir, strOutputDir.c_str(), strOutputDir.size());
    }
    else
    {
        if (FAILED(hr = UtilGetTempDirPath(szTempDir, MAX_PATH)))
            return hr;
    }

    if (IsSelf(szImageFileRessourceID))
    {
        if (FAILED(GetSelf(outputFile)))
            return hr;
        return S_OK;
    }

    wstring MotherShip, ResName, NameInArchive, FormatName;

    if (SUCCEEDED(
            hr = SplitResourceReference(pLog, szImageFileRessourceID, MotherShip, ResName, NameInArchive, FormatName)))
    {
        if (NameInArchive.empty())
        {
            // Resource is directly embedded in resource

            shared_ptr<ResourceStream> res = make_shared<ResourceStream>(pLog);

            HRSRC hRes = NULL;
            HMODULE hModule = NULL;
            std::wstring strBinaryPath;
            if (FAILED(hr = LocateResource(pLog, MotherShip, ResName, BINARY(), hModule, hRes, strBinaryPath)))
            {
                log::Verbose(pLog, L"WARNING: Could not locate resource %s\r\n", szImageFileRessourceID.c_str());
                return hr;
            }

            if (FAILED(res->OpenForReadOnly(hModule, hRes)))
                return hr;

            wstring fileName;
            HANDLE hFile = INVALID_HANDLE_VALUE;

            if (FAILED(
                    hr = UtilGetUniquePath(
                        pLog,
                        szTempDir,
                        Keyword.c_str(),
                        fileName,
                        hFile,
                        FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                        szSDDL)))
            {
                if (hr == HRESULT_FROM_WIN32(ERROR_FILE_EXISTS))
                {
                    if (FAILED(
                            hr = UtilGetUniquePath(
                                pLog,
                                szTempDir,
                                Keyword.c_str(),
                                fileName,
                                hFile,
                                FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                                szSDDL)))
                    {
                        log::Error(pLog, hr, L"Failed to create extracted file %s\r\n", fileName.c_str());
                        return hr;
                    }
                }
                else
                {
                    log::Error(pLog, hr, L"Failed to create extracted file %s\r\n", fileName.c_str());
                    return hr;
                }
            }

            shared_ptr<FileStream> fs = make_shared<FileStream>(pLog);
            if (FAILED(hr = fs->OpenHandle(hFile)))
            {
                log::Error(pLog, hr, L"Failed to create extracted file %s\r\n", fileName.c_str());
                return hr;
            }
            ULONGLONG nbBytes = 0;
            if (FAILED(hr = res->CopyTo(fs, &nbBytes)))
            {
                log::Error(pLog, hr, L"Failed to copy resource to file %s\r\n", fileName.c_str());
                return hr;
            }
            fs->Close();
            res->Close();

            outputFile.swap(fileName);
        }
        else
        {
            // Resource is based in an archive... extracting file
            auto fmt = ArchiveExtract::GetArchiveFormat(FormatName);
            if (fmt == ArchiveFormat::Unknown)
                fmt = ArchiveFormat::Cabinet;

            auto extract = ArchiveExtract::MakeExtractor(fmt, pLog);

            vector<wstring> ToExtract;

            ToExtract.push_back(NameInArchive);
            if (FAILED(hr = extract->Extract(szImageFileRessourceID.c_str(), szTempDir, szSDDL, ToExtract)))
                return hr;

            if (!extract->Items().empty())
            {
                outputFile = begin(extract->Items())->Path;
            }
            else
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }
    }
    else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MATCH))
    {
        log::Error(
            pLog,
            hr,
            L"\"%s\" does not match a typical embedded ressource pattern\r\n",
            szImageFileRessourceID.c_str());
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT
EmbeddedResource::ExtractToBuffer(const logger& pLog, const std::wstring& szImageFileRessourceID, CBinaryBuffer& Buffer)
{
    HRESULT hr = E_FAIL;

    wstring MotherShip, ResName, NameInArchive, FormatName;

    if (SUCCEEDED(
            hr = SplitResourceReference(pLog, szImageFileRessourceID, MotherShip, ResName, NameInArchive, FormatName)))
    {
        if (NameInArchive.empty())
        {
            // Resource is directly embedded in memory
            // Resource is directly embedded in resource

            shared_ptr<ResourceStream> res = make_shared<ResourceStream>(pLog);

            HRSRC hRes = NULL;
            HMODULE hModule = NULL;
            std::wstring strBinaryPath;
            if (FAILED(hr = LocateResource(pLog, MotherShip, ResName, BINARY(), hModule, hRes, strBinaryPath)))
            {
                log::Verbose(pLog, L"WARNING: Could not locate resource %s\r\n", szImageFileRessourceID.c_str());
                return hr;
            }

            HGLOBAL hFileResource = NULL;
            // First find and load the required resource
            if ((hFileResource = LoadResource(hModule, hRes)) == NULL)
            {
                log::Verbose(pLog, L"WARNING: Could not load ressource\r\n");
                return HRESULT_FROM_WIN32(GetLastError());
            }

            // Now open and map this to a disk file
            LPVOID lpData = NULL;
            if ((lpData = LockResource(hFileResource)) == NULL)
            {
                log::Verbose(pLog, L"WARNING: Could not lock ressource\r\n");
                return HRESULT_FROM_WIN32(GetLastError());
            }

            DWORD dwSize = 0L;
            if ((dwSize = SizeofResource(hModule, hRes)) == 0)
            {
                HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
                log::Error(pLog, hr, L"Could not compute ressource size\r\n");
                return hr;
            }

            if (auto hr = Buffer.SetData((LPBYTE)lpData, dwSize); FAILED(hr))
                return hr;
        }
        else
        {
            // Resource is based in an archive... extracting file
            auto fmt = ArchiveExtract::GetArchiveFormat(FormatName);
            if (fmt == ArchiveFormat::Unknown)
                fmt = ArchiveFormat::Cabinet;

            auto extract = ArchiveExtract::MakeExtractor(fmt, pLog);

            auto MakeArchiveStream = [&pLog, &MotherShip, &ResName, &szImageFileRessourceID](
                                         std::shared_ptr<ByteStream>& stream) -> HRESULT {
                HRESULT hr = E_FAIL;

                shared_ptr<ResourceStream> res = make_shared<ResourceStream>(pLog);

                HRSRC hRes = NULL;
                HMODULE hModule = NULL;
                std::wstring strBinaryPath;
                if (FAILED(hr = LocateResource(pLog, MotherShip, ResName, BINARY(), hModule, hRes, strBinaryPath)))
                {
                    log::Verbose(pLog, L"WARNING: Could not locate resource %s\r\n", szImageFileRessourceID.c_str());
                    return hr;
                }

                if (FAILED(res->OpenForReadOnly(hModule, hRes)))
                    return hr;

                stream = res;
                return S_OK;
            };

            std::shared_ptr<MemoryStream> pOutput;
            auto MakeWriteStream = [pLog, &pOutput](Archive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
                HRESULT hr = E_FAIL;

                auto stream = std::make_shared<MemoryStream>(pLog);

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

            const auto& items = extract->GetExtractedItems();
            if (items.empty())
            {
                log::Warning(
                    pLog,
                    HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                    L"Could not locate item \"%s\" in resource \"%s\"\r\n",
                    NameInArchive.c_str(),
                    ResName.c_str());
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            }
            if (items.size() > 1)
            {
                log::Warning(
                    pLog,
                    HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES),
                    L"%d items matched name \"%s\" in resource \"%s\"\r\n",
                    items.size(),
                    NameInArchive.c_str(),
                    ResName.c_str());
                return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
            }

            if (!pOutput)
            {
                log::Warning(
                    pLog,
                    HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                    L"Invalid output stream for item \"%s\" in resource \"%s\"\r\n",
                    NameInArchive.c_str(),
                    ResName.c_str());
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            }
            pOutput->GrabBuffer(Buffer);
        }
    }
    else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MATCH))
    {
        log::Error(
            pLog,
            hr,
            L"\"%s\" does not match a typical embedded ressource pattern\r\n",
            szImageFileRessourceID.c_str());
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT EmbeddedResource::ExtractValue(
    const logger& pLog,
    const std::wstring& Module,
    const std::wstring& Name,
    std::wstring& Value)
{
    HRESULT hr = E_FAIL;
    HRSRC hRes = NULL;
    HMODULE hModule = NULL;
    std::wstring strBinaryPath;

    if (FAILED(hr = LocateResource(pLog, Module, Name, VALUES(), hModule, hRes, strBinaryPath)))
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
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(pLog, hr, L"Failed to LoadResource \"%s\" in module \"%s\"\r\n", Name.c_str(), Module.c_str());
        return hr;
    }

    LPWSTR lpStr = NULL;
    if ((lpStr = (LPWSTR)LockResource(hFileResource)) == NULL)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Could not lock ressource\r\n");
        if (hModule != NULL)
            FreeLibrary(hModule);
        return hr;
    }

    std::wstring ResourceSpec;
    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hModule, hRes)) == 0)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Could not compute ressource size\r\n");
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

HRESULT EmbeddedResource::ExtractBuffer(
    const logger& pLog,
    const std::wstring& Module,
    const std::wstring& Name,
    CBinaryBuffer& Value)
{
    HRESULT hr = E_FAIL;
    HRSRC hRes = NULL;
    std::wstring strBinaryPath;
    HMODULE hModule = NULL;

    if (FAILED(hr = LocateResource(pLog, Module, Name, BINARY(), hModule, hRes, strBinaryPath)))
    {
        return hr;
    }
    else if (hModule == NULL && hRes == NULL)
    {
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    }
    HGLOBAL hFileResource = LoadResource(hModule, hRes);

    LPVOID lpVoid = NULL;

    if ((lpVoid = LockResource(hFileResource)) == NULL)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Could not lock ressource\r\n");
        if (hModule != GetDefaultHINSTANCE())
            FreeLibrary(hModule);
        return hr;
    }

    CBinaryBuffer retval;
    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hModule, hRes)) == 0)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Could not compute ressource size\r\n");
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

    V_EnumResourceNamesEx_Param(
        logger plog,
        const std::wstring& strModule,
        std::vector<EmbeddedResource::EmbedSpec>& values)
        : pLog(plog)
        , Module(strModule)
        , Values(values) {};

    logger pLog;
    const std::wstring& Module;
    std::vector<EmbeddedResource::EmbedSpec>& Values;
};

BOOL CALLBACK V_EnumResourceNamesEx_Function(HMODULE, LPCWSTR, LPWSTR lpszName, LONG_PTR lParam)
{

    HRESULT hr = E_FAIL;

    V_EnumResourceNamesEx_Param* param = (V_EnumResourceNamesEx_Param*)lParam;

    wstring strName(lpszName);
    wstring strValue;

    if (FAILED(hr = EmbeddedResource::ExtractValue(param->pLog, param->Module, strName, strValue)))
    {
        log::Error(param->pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to extract value %s\r\n", lpszName);
        return TRUE;
    }

    param->Values.emplace_back(EmbeddedResource::EmbedSpec::AddNameValuePair(std::move(strName), std::move(strValue)));

    return TRUE;
}

HRESULT EmbeddedResource::EnumValues(const logger& pLog, const std::wstring& Module, std::vector<EmbedSpec>& values)
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
            log::Error(
                pLog,
                hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                L"Ressource file %s does not exist\r\n",
                Module.c_str());
            return hr;
        }

        hInstance = LoadLibraryEx(Module.c_str(), NULL, dwLoadLibraryFlags);
        if (hInstance == NULL)
        {
            log::Error(
                pLog,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Ressource file %s could not be loaded\r\n",
                Module.c_str());
            return hr;
        }
    }

    V_EnumResourceNamesEx_Param param = {pLog, Module, values};

    const auto pExt = ExtensionLibrary::GetLibrary<Kernel32Extension>(pLog);

    if (FAILED(hr = pExt->EnumResourceNamesW(hInstance, VALUES(), V_EnumResourceNamesEx_Function, (ULONG_PTR)&param)))
    {
        log::Error(pLog, hr, L"Failed to enumerate values ressources\r\n");
        return hr;
    }

    return S_OK;
}

struct B_EnumResourceNamesEx_Param
{
    B_EnumResourceNamesEx_Param(
        logger pLog,
        const std::wstring& strModule,
        std::vector<EmbeddedResource::EmbedSpec>& values)
        : _L_(pLog)
        , Module(strModule)
        , Values(values) {};
    logger _L_;
    const std::wstring& Module;
    std::vector<EmbeddedResource::EmbedSpec>& Values;
};

BOOL CALLBACK B_EnumResourceNamesEx_Function(HMODULE, LPCWSTR, LPWSTR lpszName, LONG_PTR lParam)
{

    HRESULT hr = E_FAIL;

    B_EnumResourceNamesEx_Param* param = (B_EnumResourceNamesEx_Param*)lParam;

    wstring strName(lpszName);
    CBinaryBuffer buffer;

    if (FAILED(hr = EmbeddedResource::ExtractBuffer(param->_L_, param->Module, strName, buffer)))
    {
        log::Error(param->_L_, hr, L"Failed to extract binary ressource %s\r\n", lpszName);
        return TRUE;
    }
    param->Values.push_back(EmbeddedResource::EmbedSpec::AddBinary(std::move(strName), std::move(buffer)));

    return TRUE;
}

HRESULT EmbeddedResource::EnumBinaries(const logger& pLog, const std::wstring& Module, std::vector<EmbedSpec>& values)
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
            log::Error(
                pLog,
                hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                L"Ressource file %s does not exist\r\n",
                Module.c_str());
            return hr;
        }

        hInstance = LoadLibraryEx(Module.c_str(), NULL, dwLoadLibraryFlags);
        if (hInstance == NULL)
        {
            log::Error(
                pLog,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Ressource file %s could not be loaded\r\n",
                Module.c_str());
            return hr;
        }
    }

    B_EnumResourceNamesEx_Param param = {pLog, Module, values};

    const auto pExt = ExtensionLibrary::GetLibrary<Kernel32Extension>(pLog);

    if (FAILED(hr = pExt->EnumResourceNamesW(hInstance, BINARY(), B_EnumResourceNamesEx_Function, (ULONG_PTR)&param)))
    {
        log::Error(pLog, hr, L"Failed to enumerate values ressources\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT EmbeddedResource::ExpandArchivesAndBinaries(
    const logger& pLog,
    const std::wstring& outDir,
    std::vector<EmbedSpec>& values)
{
    HRESULT hr = E_FAIL;

    std::vector<EmbedSpec> archives;
    std::vector<EmbedSpec> binaries;

    static const CHAR cabSignature[] = "MSCF";
    static const CHAR _7zSignature[] = "7z";
    static const size_t cabSigLen = strlen(cabSignature);
    static const size_t _7zSigLen = strlen(_7zSignature);

    for (auto& item : values)
    {

        std::wstring strArchFormat;

        if (item.Type == EmbedSpec::Buffer)
        {
            bool bArchive = true;
            if (item.BinaryValue.GetCount() > cabSigLen)
            {
                for (size_t i = 0; i < cabSigLen; i++)
                {
                    if (item.BinaryValue.Get<CHAR>(i) != cabSignature[i])
                        bArchive = false;
                }
                if (bArchive)
                    strArchFormat = L"cab";
            }
            else
                bArchive = false;

            if (!bArchive)
            {
                bArchive = true;
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
            }

            if (bArchive)
            {
                item.ArchiveFormat = strArchFormat;
                archives.push_back(item);
                item.Type = EmbedSpec::Void;
            }
            else
            {
                item.Type = EmbedSpec::File;
                binaries.push_back(item);
                item.Type = EmbedSpec::Void;
            }
        }
    }

    auto new_end =
        std::remove_if(begin(values), end(values), [](const EmbedSpec& item) { return item.Type == EmbedSpec::Void; });
    values.erase(new_end, end(values));

    for (const auto& item : binaries)
    {
        auto inBinary = std::make_shared<MemoryStream>(pLog);

        if (FAILED(hr = inBinary->OpenForReadOnly(item.BinaryValue.GetData(), item.BinaryValue.GetCount())))
        {
            log::Error(pLog, hr, L"Failed to open memory stream for ressource\r\n");
            continue;
        }

        wstring out = outDir + L"\\" + item.Name;

        auto outFile = std::make_shared<FileStream>(pLog);

        if (FAILED(hr = outFile->WriteTo(out.c_str())))
        {
            log::Error(pLog, hr, L"Failed to create output file %s\r\n", out.c_str());
            continue;
        }
        ULONGLONG cbWritten = 0LL;

        if (FAILED(hr = inBinary->CopyTo(outFile, &cbWritten)))
        {
            log::Error(pLog, hr, L"Failed to write ressource content to output file %s\r\n", out.c_str());
            continue;
        }
        values.push_back(EmbedSpec::AddFile(item.Name, out));
    }

    for (const auto& item : archives)
    {
        auto archStream = std::make_shared<MemoryStream>(pLog);

        if (FAILED(hr = archStream->OpenForReadOnly(item.BinaryValue.GetData(), item.BinaryValue.GetCount())))
        {
            log::Error(pLog, hr, L"Failed to open memory stream for ressource\r\n");
            continue;
        }

        auto extractor = ArchiveExtract::MakeExtractor(Archive::GetArchiveFormat(item.ArchiveFormat), pLog);
        if (!extractor)
        {
            log::Error(pLog, hr, L"Failed to create extractor for ressource\r\n");
            continue;
        }

        wstring out = outDir + L"\\" + item.Name;

        fs::create_directories(fs::path(out));

        if (FAILED(hr = extractor->Extract(archStream, out.c_str(), nullptr)))
        {
            log::Error(pLog, hr, L"Failed to extract ressource into %s\r\n", out.c_str());
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

HRESULT EmbeddedResource::DeleteEmbeddedRessources(
    const logger& pLog,
    const std::wstring& inModule,
    const std::wstring& outModule,
    std::vector<EmbedSpec>& values)
{
    HRESULT hr = E_FAIL;
    std::vector<EmbedSpec> to_delete;

    if (values.empty())
    {
        if (FAILED(hr = EnumValues(pLog, inModule, to_delete)))
        {
            log::Error(pLog, hr, L"Failed to enumerate ressources to delete\r\n");
            return hr;
        }
        if (FAILED(hr = EnumBinaries(pLog, inModule, to_delete)))
        {
            log::Error(pLog, hr, L"Failed to enumerate ressources to delete\r\n");
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

    auto inStream = std::make_shared<FileStream>(pLog);
    std::wstring inputFile;

    if (inModule.empty())
    {
        if (FAILED(hr = GetSelf(inputFile)))
        {
            log::Error(pLog, hr, L"Failed to get my own path\r\n", inputFile.c_str());
            return hr;
        }
    }
    else
    {
        inputFile = inModule;
    }

    if (FAILED(hr = inStream->ReadFrom(inputFile.c_str())))
    {
        log::Error(pLog, hr, L"Failed to open input binary %s to update ressources\r\n", inputFile.c_str());
        return hr;
    }

    auto outStream = std::make_shared<FileStream>(pLog);
    if (FAILED(
            hr = outStream->OpenFile(
                outModule.c_str(), GENERIC_WRITE, 0L, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)))
    {
        log::Error(pLog, hr, L"Failed to open output binary %s to update ressources\r\n", outModule.c_str());
        return hr;
    }
    ULONGLONG ullWritten = 0LL;
    ;

    if (FAILED(hr = inStream->CopyTo(outStream, &ullWritten)))
    {
        log::Error(pLog, hr, L"Failed to copy %s to %s\r\n", inputFile.c_str(), outModule.c_str());
        return hr;
    }
    inStream->Close();
    outStream->Close();

    if (FAILED(hr = UpdateResources(pLog, outModule, to_delete)))
    {
        log::Error(pLog, hr, L"Failed to update ressources in %s\r\n", outModule.c_str());
        return hr;
    }

    return S_OK;
}

EmbeddedResource::~EmbeddedResource(void) {}
