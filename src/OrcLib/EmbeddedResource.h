//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <regex>

#include "BinaryBuffer.h"

#include "Archive.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API EmbeddedResource
{
public:
    class EmbedSpec
    {

    public:
        class ArchiveItem
        {
        public:
            std::wstring Name;
            std::wstring Path;
        };

        typedef enum _EmbedType
        {
            NameValuePair,
            File,
            Archive,
            Buffer,
            BinaryDeletion,
            ValuesDeletion,
            Void = -1
        } EmbedType;

        EmbedType Type;
        std::wstring Name;
        std::wstring Value;
        CBinaryBuffer BinaryValue;

        std::vector<ArchiveItem> ArchiveItems;
        std::wstring ArchiveFormat;
        std::wstring ArchiveCompression;

    private:
        EmbedSpec() { Type = Void; };

    public:
        EmbedSpec(const EmbedSpec& anOther) = default;

        EmbedSpec(EmbedSpec&& anOther) noexcept = default;

        EmbedSpec& operator=(const EmbedSpec&) = default;

        static EmbedSpec AddNameValuePair(const std::wstring& Name, const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = Name;
            retval.Value = Value;

            return retval;
        };
        static EmbedSpec AddNameValuePair(std::wstring&& Name, std::wstring&& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            std::swap(retval.Name, Name);
            std::swap(retval.Value, Value);
            return retval;
        };
        static EmbedSpec AddBinary(std::wstring&& Name, CBinaryBuffer&& Value)
        {
            EmbedSpec retval;
            retval.Type = Buffer;
            std::swap(retval.Name, Name);
            std::swap(retval.BinaryValue, Value);
            return retval;
        };
        static EmbedSpec AddBinary(const std::wstring& Name, const CBinaryBuffer& Value)
        {
            EmbedSpec retval;
            retval.Type = Buffer;
            retval.Name = Name;
            retval.BinaryValue = Value;
            return retval;
        };

        static EmbedSpec AddFile(const std::wstring& Name, const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = File;
            retval.Name = Name;
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddFile(std::wstring&& Name, std::wstring&& Value)
        {
            EmbedSpec retval;
            retval.Type = File;
            std::swap(retval.Name, Name);
            std::swap(retval.Value, Value);
            return retval;
        };
        static EmbedSpec AddArchive(const std::wstring& Name, const std::vector<ArchiveItem>& Value)
        {
            EmbedSpec retval;
            retval.Type = Archive;
            retval.Name = Name;
            retval.ArchiveItems = Value;
            retval.ArchiveFormat = Archive::GetArchiveFormatString(Archive::GetArchiveFormat(Name));
            return retval;
        };
        static EmbedSpec AddArchive(
            const std::wstring& Name,
            const std::wstring& Format,
            const std::wstring& Compression,
            const std::vector<ArchiveItem>& Value)
        {
            EmbedSpec retval;
            retval.Type = Archive;
            retval.Name = Name;
            retval.ArchiveFormat = Format;
            retval.ArchiveCompression = Compression;
            retval.ArchiveItems = Value;
            return retval;
        };
        static EmbedSpec AddArchive(
            std::wstring&& Name,
            std::wstring&& Format,
            std::wstring&& Compression,
            std::vector<ArchiveItem>&& Value)
        {
            EmbedSpec retval;
            retval.Type = Archive;
            std::swap(retval.Name, Name);
            std::swap(retval.ArchiveFormat, Format);
            std::swap(retval.ArchiveCompression, Compression);
            std::swap(retval.ArchiveItems, Value);
            return retval;
        };
        static EmbedSpec AddRun(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN";
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRun(std::wstring&& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN";
            std::swap(retval.Value, Value);
            return retval;
        };
        static EmbedSpec AddRunX86(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN32";
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRunX86(std::wstring&& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN32";
            std::swap(retval.Value, Value);
            return retval;
        };
        static EmbedSpec AddRunX64(std::wstring&& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN64";
            std::swap(retval.Value, Value);
            return retval;
        };
        static EmbedSpec AddRunX64(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN64";
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRunArgs(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN_ARGS";
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRun32Args(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN32_ARGS";
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRun64Args(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = NameValuePair;
            retval.Name = L"RUN64_ARGS";
            retval.Value = Value;
            return retval;
        };

        static EmbedSpec AddValuesDeletion(const std::wstring& Name)
        {
            EmbedSpec retval;
            retval.Type = ValuesDeletion;
            retval.Name = Name;
            return retval;
        };
        static EmbedSpec AddValuesDeletion(std::wstring&& Name)
        {
            EmbedSpec retval;
            retval.Type = ValuesDeletion;
            std::swap(retval.Name, Name);
            return retval;
        };
        static EmbedSpec AddBinaryDeletion(const std::wstring& Name)
        {
            EmbedSpec retval;
            retval.Type = BinaryDeletion;
            retval.Name = Name;
            return retval;
        };
        static EmbedSpec AddBinaryDeletion(std::wstring&& Name)
        {
            EmbedSpec retval;
            retval.Type = BinaryDeletion;
            std::swap(retval.Name, Name);
            return retval;
        };
        static EmbedSpec AddVoid() { return EmbedSpec(); };

        void DeleteMe()
        {
            if (Type == NameValuePair)
                Type = ValuesDeletion;
            else
                Type = BinaryDeletion;
            Value.clear();
            BinaryValue.RemoveAll();
            ArchiveItems.clear();
            ArchiveCompression.clear();
            ArchiveFormat.clear();
        };
    };

private:
    static HRESULT _UpdateResource(
        const logger& pLog,
        HANDLE hOutput,
        const WCHAR* szModule,
        const WCHAR* szType,
        const WCHAR* szName,
        LPVOID pData,
        DWORD cbSize);

    static std::wregex& ArchRessourceRegEx();
    static std::wregex& ResRessourceRegEx();
    static std::wregex& SelfReferenceRegEx();

    static HINSTANCE GetDefaultHINSTANCE();

public:
    EmbeddedResource(void);

    static void SetDefaultHINSTANCE(HINSTANCE hInstance);

    static const WCHAR* VALUES();
    static const WCHAR* BINARY();

    static bool IsResourceBasedArchiveFile(const WCHAR* szCabFileName);

    static bool IsResourceBased(const std::wstring& szImageFileRessourceID);

    static bool IsSelf(const std::wstring& szImageFileRessourceID);

    static HRESULT GetSelfArgument(const std::wstring& strRef, std::wstring& strArg);

    static bool BuildResourceBasedName(
        const std::wstring& HostBinary,
        const std::wstring& ResName,
        WCHAR* szCabName,
        DWORD dwCabNameLen)
    {
        swprintf_s(szCabName, dwCabNameLen, L"res:%s#%s", HostBinary.c_str(), ResName.c_str());
        return true;
    }

    static bool BuildResourceBasedArchivedName(
        const std::wstring& FormatName,
        const std::wstring& HostBinary,
        const std::wstring& ResName,
        const WCHAR* szCabbedName,
        WCHAR* szCabName,
        DWORD dwCabNameLen)
    {
        swprintf_s(
            szCabName,
            dwCabNameLen,
            L"%s:%s#%s|%s",
            FormatName.c_str(),
            HostBinary.c_str(),
            ResName.c_str(),
            szCabbedName);
        return true;
    }

    static HRESULT SplitResourceReference(
        const logger& pLog,
        const std::wstring& Ref,
        std::wstring& HostBinary,
        std::wstring& ResName,
        std::wstring& NameInArchive,
        std::wstring& FormatName);

    static HRESULT LocateResource(
        const logger& pLog,
        const std::wstring& HostBinary,
        const std::wstring& ResName,
        const WCHAR* szResType,
        HMODULE& hModule,
        HRSRC& hRes,
        std::wstring& strModuleFileName);

    static HRESULT
    UpdateResources(const logger& pLog, const std::wstring& strPEToUpdate, const std::vector<EmbedSpec>& ToEmbed);

    static HRESULT GetSelf(std::wstring& outputFile);

    static HRESULT ExtractToFile(
        const logger& pLog,
        const std::wstring& szImageFileRessourceID,
        const std::wstring& Keyword,
        LPCWSTR szSDDLFormat,
        LPCWSTR szSID,
        const std::wstring& strTempDir,
        std::wstring& outputFile);
    static HRESULT ExtractToFile(
        const logger& pLog,
        const std::wstring& szImageFileRessourceID,
        const std::wstring& Keyword,
        LPCWSTR szSDDL,
        const std::wstring& strTempDir,
        std::wstring& outputFile);

    static HRESULT
    ExtractToBuffer(const logger& pLog, const std::wstring& szImageFileRessourceID, CBinaryBuffer& Buffer);

    template <typename _T, size_t _DeclElts>
    static HRESULT
    ExtractToBuffer(const logger& pLog, const std::wstring& szImageFileRessourceID, Buffer<_T, _DeclElts>& Buffer);

    static HRESULT
    ExtractValue(const logger& pLog, const std::wstring& Module, const std::wstring& Name, std::wstring& Value);
    static HRESULT
    ExtractBuffer(const logger& pLog, const std::wstring& Module, const std::wstring& Name, CBinaryBuffer& Value);

    static HRESULT ExtractRun(const logger& pLog, const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(pLog, Module, std::wstring(L"RUN"), Value);
    }
    static HRESULT ExtractRun32(const logger& pLog, const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(pLog, Module, std::wstring(L"RUN32"), Value);
    }
    static HRESULT ExtractRun64(const logger& pLog, const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(pLog, Module, std::wstring(L"RUN64"), Value);
    }

    static HRESULT ExtractRunWithArgs(
        const logger& pLog,
        const std::wstring& Module,
        std::wstring& strToExecuteRef,
        std::wstring& strRunArgs);
    static bool IsConfiguredToRun(const logger& pLog);

    static HRESULT ExtractRunArgs(const logger& pLog, const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(pLog, Module, std::wstring(L"RUN_ARGS"), Value);
    }

    static HRESULT ExtractRun32Args(const logger& pLog, const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(pLog, Module, std::wstring(L"RUN32_ARGS"), Value);
    }

    static HRESULT ExtractRun64Args(const logger& pLog, const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(pLog, Module, std::wstring(L"RUN64_ARGS"), Value);
    }

    static HRESULT EnumValues(const logger& pLog, const std::wstring& Module, std::vector<EmbedSpec>& values);
    static HRESULT EnumBinaries(const logger& pLog, const std::wstring& Module, std::vector<EmbedSpec>& values);

    static HRESULT
    ExpandArchivesAndBinaries(const logger& pLog, const std::wstring& outDir, std::vector<EmbedSpec>& values);

    static HRESULT DeleteEmbeddedRessources(
        const logger& pLog,
        const std::wstring& inModule,
        const std::wstring& outModule,
        std::vector<EmbedSpec>& values);

    ~EmbeddedResource(void);
};

static constexpr LPCWSTR RESSOURCE_READ_ONLY_SID = L"D:PAI(A;;0x130081;;;%s)";
static constexpr LPCWSTR RESSOURCE_READ_EXECUTE_SID = L"D:PAI(A;;0x1300a9;;;%s)";
static constexpr LPCWSTR RESSOURCE_FULL_CONTROL_SID = L"D:PAI(A;;FA;;;%s)";
static constexpr LPCWSTR RESSOURCE_GENERIC_READ_SID = L"D:PAI(A;;FR;;;%s)";

static constexpr LPCWSTR RESSOURCE_READ_ONLY_BA = L"D:PAI(A;;0x130081;;;BA)";
static constexpr LPCWSTR RESSOURCE_READ_EXECUTE_BA = L"D:PAI(A;;0x1300a9;;;BA)";
static constexpr LPCWSTR RESSOURCE_FULL_CONTROL_BA = L"D:PAI(A;;FA;;;BA)";
static constexpr LPCWSTR RESSOURCE_GENERIC_READ_BA = L"D:PAI(A;;FR;;;BA)";

}  // namespace Orc

#include "ArchiveExtract.h"
#include "ResourceStream.h"

template <typename _T, size_t _DeclElts>
static HRESULT Orc::EmbeddedResource::ExtractToBuffer(
    const logger& pLog,
    const std::wstring& szImageFileRessourceID,
    Buffer<_T, _DeclElts>& Buffer)
{
    using namespace std;
    using namespace msl::utilities;

    auto hr = E_FAIL;

    std::wstring MotherShip, ResName, NameInArchive, FormatName;

    if (SUCCEEDED(
            hr = SplitResourceReference(pLog, szImageFileRessourceID, MotherShip, ResName, NameInArchive, FormatName)))
    {
        if (NameInArchive.empty())
        {
            // Resource is directly embedded in memory
            // Resource is directly embedded in resource

            std::shared_ptr<ResourceStream> res = std::make_shared<ResourceStream>(pLog);

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
                auto hr = HRESULT_FROM_WIN32(GetLastError());
                log::Error(pLog, hr, L"Could not compute ressource size\r\n");
                return hr;
            }

            if ((dwSize % sizeof(_T)) > 0)
            {
                auto hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                log::Error(pLog, hr, L"Resource's size is not compatible with buffer's type\r\n");
                return hr;
            }

            Buffer.assign(reinterpret_cast<_T*>(lpData), dwSize / sizeof(_T));
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

            CBinaryBuffer itembuffer;
            pOutput->GrabBuffer(itembuffer);

            if ((itembuffer.GetCount() % sizeof(_T)) > 0)
            {
                auto hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                log::Error(pLog, hr, L"Uncompressed resource's size is not compatible with buffer's type\r\n");
                return hr;
            }
            Buffer.assign(
                reinterpret_cast<_T*>(itembuffer.GetData()), SafeInt<ULONG>(itembuffer.GetCount() / sizeof(_T)));
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

#pragma managed(pop)
