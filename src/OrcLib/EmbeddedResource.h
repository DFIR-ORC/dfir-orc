//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "BinaryBuffer.h"
#include "Archive.h"

#include "Log/Log.h"
#include "Utils/Result.h"

#include <regex>

#include <safeint.h>

#pragma managed(push, off)

namespace Orc {

using namespace std::string_view_literals;

class EmbeddedResource
{
public:
    static constexpr auto RUN_FMT = L"RUN{}"sv;
    static constexpr auto RUN_ARGS_FMT = L"RUN{}_ARGS"sv;
    static constexpr auto _32 = L"32"sv;
    static constexpr auto _64 = L"64"sv;
    static constexpr auto _ARM64 = L"_ARM64"sv;

    static auto RUN() { return fmt::format(RUN_FMT, L""sv); }
    static auto RUN_32() { return fmt::format(RUN_FMT, _32); }
    static auto RUN_64() { return fmt::format(RUN_FMT, _64); }
    static auto RUN_ARM64() { return fmt::format(RUN_FMT, _ARM64); }

    static auto RUN_ARGS() { return fmt::format(RUN_ARGS_FMT, L""sv); }
    static auto RUN_32_ARGS() { return fmt::format(RUN_ARGS_FMT, _32); }
    static auto RUN_64_ARGS() { return fmt::format(RUN_ARGS_FMT, _64); }
    static auto RUN_ARM64_ARGS() { return fmt::format(RUN_ARGS_FMT, _ARM64); }

    class EmbedSpec
    {

    public:
        struct ArchiveItem
        {
        public:
            std::wstring Name;
            std::wstring Path;
        };

        enum class EmbedType
        {
            NameValuePair,
            File,
            Archive,
            Buffer,
            BinaryDeletion,
            ValuesDeletion,
            Void = -1
        };

        EmbedType Type;
        std::wstring Name;
        std::wstring Value;
        CBinaryBuffer BinaryValue;

        std::vector<ArchiveItem> ArchiveItems;
        std::wstring ArchiveFormat;
        std::wstring ArchiveCompression;

    private:
        EmbedSpec() { Type = EmbedType::Void; };

    public:
        EmbedSpec(const EmbedSpec& anOther) = default;

        EmbedSpec(EmbedSpec&& anOther) noexcept = default;

        EmbedSpec& operator=(const EmbedSpec&) = default;

        static EmbedSpec AddNameValuePair(const std::wstring& Name, const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = Name;
            retval.Value = Value;

            return retval;
        };
        static EmbedSpec AddNameValuePair(std::wstring&& Name, std::wstring&& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            std::swap(retval.Name, Name);
            std::swap(retval.Value, Value);
            return retval;
        };
        static EmbedSpec AddBinary(std::wstring&& Name, CBinaryBuffer&& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::Buffer;
            std::swap(retval.Name, Name);
            std::swap(retval.BinaryValue, Value);
            return retval;
        };
        static EmbedSpec AddBinary(const std::wstring& Name, const CBinaryBuffer& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::Buffer;
            retval.Name = Name;
            retval.BinaryValue = Value;
            return retval;
        };

        static EmbedSpec AddFile(const std::wstring& Name, const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::File;
            retval.Name = Name;
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddFile(std::wstring&& Name, std::wstring&& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::File;
            std::swap(retval.Name, Name);
            std::swap(retval.Value, Value);
            return retval;
        };
        static EmbedSpec AddArchive(const std::wstring& Name, const std::vector<ArchiveItem>& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::Archive;
            retval.Name = Name;
            retval.ArchiveItems = Value;
            retval.ArchiveFormat = OrcArchive::GetArchiveFormatString(OrcArchive::GetArchiveFormat(Name));
            return retval;
        };
        static EmbedSpec AddArchive(
            const std::wstring& Name,
            const std::wstring& Format,
            const std::wstring& Compression,
            const std::vector<ArchiveItem>& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::Archive;
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
            retval.Type = EmbedType::Archive;
            std::swap(retval.Name, Name);
            std::swap(retval.ArchiveFormat, Format);
            std::swap(retval.ArchiveCompression, Compression);
            std::swap(retval.ArchiveItems, Value);
            return retval;
        };
        static EmbedSpec AddRun(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN();
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRunX86(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN_32();
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRunX64(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN_64();
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRun_ARM64(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN_ARM64();
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRunArgs(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN_ARGS();
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRun32Args(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN_32_ARGS();
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRun64Args(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN_64_ARGS();
            retval.Value = Value;
            return retval;
        };
        static EmbedSpec AddRun_ARM64_Args(const std::wstring& Value)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::NameValuePair;
            retval.Name = RUN_ARM64_ARGS();
            retval.Value = Value;
            return retval;
        };

        static EmbedSpec AddValuesDeletion(const std::wstring& Name)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::ValuesDeletion;
            retval.Name = Name;
            return retval;
        };
        static EmbedSpec AddValuesDeletion(std::wstring&& Name)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::ValuesDeletion;
            std::swap(retval.Name, Name);
            return retval;
        };
        static EmbedSpec AddBinaryDeletion(const std::wstring& Name)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::BinaryDeletion;
            retval.Name = Name;
            return retval;
        };
        static EmbedSpec AddBinaryDeletion(std::wstring&& Name)
        {
            EmbedSpec retval;
            retval.Type = EmbedType::BinaryDeletion;
            std::swap(retval.Name, Name);
            return retval;
        };
        static EmbedSpec AddVoid() { return EmbedSpec(); };

        void DeleteMe()
        {
            if (Type == EmbedType::NameValuePair)
                Type = EmbedType::ValuesDeletion;
            else
                Type = EmbedType::BinaryDeletion;
            Value.clear();
            BinaryValue.RemoveAll();
            ArchiveItems.clear();
            ArchiveCompression.clear();
            ArchiveFormat.clear();
        };
    };

private:
    static HRESULT _UpdateResource(
        HANDLE hOutput,
        const WCHAR* szModule,
        const WCHAR* szType,
        const WCHAR* szName,
        LPVOID pData,
        DWORD cbSize);

    static std::wregex& ArchResourceRegEx();
    static std::wregex& ResResourceRegEx();
    static std::wregex& SelfReferenceRegEx();

    static HINSTANCE GetDefaultHINSTANCE();

public:
    EmbeddedResource(void);

    static void SetDefaultHINSTANCE(HINSTANCE hInstance);

    static const WCHAR* VALUES();
    static const WCHAR* BINARY();

    static bool IsResourceBasedArchiveFile(const WCHAR* szCabFileName);

    static bool IsResourceBased(const std::wstring& szImageFileResourceID);

    static bool IsSelf(const std::wstring& szImageFileResourceID);

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
        const std::wstring& Ref,
        std::wstring& HostBinary,
        std::wstring& ResName,
        std::wstring& NameInArchive,
        std::wstring& FormatName);

    static HRESULT LocateResource(
        const std::wstring& HostBinary,
        const std::wstring& ResName,
        const WCHAR* szResType,
        HMODULE& hModule,
        HRSRC& hRes,
        std::wstring& strModuleFileName);

    static HRESULT UpdateResources(const std::wstring& strPEToUpdate, const std::vector<EmbedSpec>& ToEmbed);

    static HRESULT GetSelf(std::wstring& outputFile);

    static HRESULT ExtractToFile(
        const std::wstring& szImageFileResourceID,
        const std::wstring& Keyword,
        LPCWSTR szSDDLFormat,
        LPCWSTR szSID,
        const std::wstring& strTempDir,
        std::wstring& outputFile);
    static HRESULT ExtractToFile(
        const std::wstring& szImageFileResourceID,
        const std::wstring& Keyword,
        LPCWSTR szSDDL,
        const std::wstring& strTempDir,
        std::wstring& outputFile);

    static HRESULT ExtractToDirectory(
        const std::wstring& szImageFileResourceID,
        const std::wstring& Keyword,
        LPCWSTR szSDDL,
        const std::wstring& strTempDir,
        std::vector<std::pair<std::wstring, std::wstring>>& outputFiles);

    static HRESULT ExtractToDirectory(
        const std::wstring& szImageFileResourceID,
        const std::wstring& Keyword,
        LPCWSTR szSDDLFormat,
        LPCWSTR szSID,
        const std::wstring& strTempDir,
        std::vector<std::pair<std::wstring, std::wstring>>& outputFiles);

    static HRESULT ExtractToBuffer(const std::wstring& szImageFileResourceID, CBinaryBuffer& Buffer);

    template <typename _T, size_t _DeclElts>
    static HRESULT ExtractToBuffer(const std::wstring& szImageFileResourceID, Buffer<_T, _DeclElts>& Buffer);

    static HRESULT ExtractValue(const std::wstring& Module, const std::wstring& Name, std::wstring& Value);
    static HRESULT ExtractBuffer(const std::wstring& Module, const std::wstring& Name, CBinaryBuffer& Value);

    static HRESULT ExtractRun(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, fmt::format(RUN_FMT, L""), Value);
    }
    static HRESULT ExtractRun32(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, fmt::format(RUN_FMT, _32), Value);
    }
    static HRESULT ExtractRun64(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, fmt::format(RUN_FMT, _64), Value);
    }
    static HRESULT ExtractRunARM64(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, fmt::format(RUN_FMT, _ARM64), Value);
    }

    static HRESULT
    ExtractRunWithArgs(const std::wstring& Module, std::wstring& strToExecuteRef, std::wstring& strRunArgs);
    static bool IsConfiguredToRun();

    static HRESULT ExtractRunArgs(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, RUN_ARGS(), Value);
    }

    static HRESULT ExtractRun32Args(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, RUN_32_ARGS(), Value);
    }

    static HRESULT ExtractRun64Args(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, RUN_64_ARGS(), Value);
    }

    static HRESULT ExtractRunARM64Args(const std::wstring& Module, std::wstring& Value)
    {
        return ExtractValue(Module, RUN_ARM64_ARGS(), Value);
    }

    static HRESULT EnumValues(const std::wstring& Module, std::vector<EmbedSpec>& values);
    static HRESULT EnumBinaries(const std::wstring& Module, std::vector<EmbedSpec>& values);

    static HRESULT ExpandArchivesAndBinaries(const std::wstring& outDir, std::vector<EmbedSpec>& values);

    static HRESULT DeleteEmbeddedResources(
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
HRESULT Orc::EmbeddedResource::ExtractToBuffer(const std::wstring& szImageFileResourceID, Buffer<_T, _DeclElts>& Buffer)
{
    using namespace std;
    using namespace msl::utilities;

    auto hr = E_FAIL;

    std::wstring MotherShip, ResName, NameInArchive, FormatName;

    if (SUCCEEDED(hr = SplitResourceReference(szImageFileResourceID, MotherShip, ResName, NameInArchive, FormatName)))
    {
        if (NameInArchive.empty())
        {
            // Resource is directly embedded in memory
            // Resource is directly embedded in resource

            std::shared_ptr<ResourceStream> res = std::make_shared<ResourceStream>();

            HRSRC hRes = NULL;
            HMODULE hModule = NULL;
            std::wstring strBinaryPath;
            if (FAILED(hr = LocateResource(MotherShip, ResName, BINARY(), hModule, hRes, strBinaryPath)))
            {
                Log::Warn(L"Could not locate resource {} [{}]", szImageFileResourceID, SystemError(hr));
                return hr;
            }

            HGLOBAL hFileResource = NULL;
            // First find and load the required resource
            if ((hFileResource = LoadResource(hModule, hRes)) == NULL)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Warn(L"Could not load resource [{}]", SystemError(hr));
                return hr;
            }

            // Now open and map this to a disk file
            LPVOID lpData = NULL;
            if ((lpData = LockResource(hFileResource)) == NULL)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Warn(L"Could not lock resource [{}]", SystemError(hr));
                return hr;
            }

            DWORD dwSize = 0L;
            if ((dwSize = SizeofResource(hModule, hRes)) == 0)
            {
                auto hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Error(L"Could not compute resource size [{}]", SystemError(hr));
                return hr;
            }

            if ((dwSize % sizeof(_T)) > 0)
            {
                auto hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                Log::Error(L"Resource's size is not compatible with buffer's type [{}]", SystemError(hr));
                return hr;
            }

            Buffer.assign(reinterpret_cast<_T*>(lpData), dwSize / sizeof(_T));
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
                    Log::Warn(L"Could not locate resource [{}]", szImageFileResourceID, SystemError(hr));
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
                    if (FAILED(hr = stream->OpenForReadWrite(4096, SafeInt<DWORD>(item.Size))))
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
                Log::Warn(L"Could not locate item '{}' in resource '{}'", NameInArchive, ResName);
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            }

            if (items.size() > 1)
            {
                Log::Warn(L"{} items matched name '{}' in resource '{}'", items.size(), NameInArchive, ResName);
                return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
            }

            if (!pOutput)
            {
                Log::Warn(L"Invalid output stream for item '{}' in resource '{}'", NameInArchive, ResName);
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            }

            CBinaryBuffer itembuffer;
            pOutput->GrabBuffer(itembuffer);

            if ((itembuffer.GetCount() % sizeof(_T)) > 0)
            {
                Log::Error(L"Uncompressed resource's size is not compatible with buffer's type");
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }

            Buffer.assign(
                reinterpret_cast<_T*>(itembuffer.GetData()), SafeInt<ULONG>(itembuffer.GetCount() / sizeof(_T)));
        }
    }
    else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MATCH))
    {
        Log::Error(L"'{}' does not match a typical embedded resource pattern", szImageFileResourceID);
        return E_INVALIDARG;
    }

    return S_OK;
}

#pragma managed(pop)
