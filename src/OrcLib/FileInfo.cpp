//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "FileInfo.h"
#include "SystemDetails.h"

#include "CryptoHashStream.h"
#include "FuzzyHashStream.h"
#include "MemoryStream.h"
#include "MFTRecordFileInfo.h"
#include "VolumeReader.h"

#include "TableOutput.h"
#include "TableOutputWriter.h"

#include <boost/scope_exit.hpp>

#include <sstream>
#include <iomanip>

#pragma comment(lib, "Crypt32.lib")

#include "Log/Log.h"

#ifndef IMAGE_FILE_MACHINE_TARGET_HOST
#    define IMAGE_FILE_MACHINE_TARGET_HOST 0x0001
#endif

#ifndef IMAGE_FILE_MACHINE_ARM64
#    define IMAGE_FILE_MACHINE_ARM64 0xAA64
#endif

#ifndef IMAGE_FILE_MACHINE_ARMNT
#    define IMAGE_FILE_MACHINE_ARMNT 0x01C4
#endif

//
// Those value are undocumented, they are coming from Microsoft runtimes from:
// \\Program Files\\WindowsPowerShell\\Modules\\MicrosoftTeams\\2.3.1\\netcoreapp3.1\\runtimes
//
#define IMAGE_FILE_MACHINE_OSX 0xC020
#define IMAGE_FILE_MACHINE_LINUX 0xFD1D  // TODO: be more specific (linux-x64, ...)

namespace {

Orc::Result<Orc::CBinaryBuffer> ReadExtendedAttribute(
    const std::shared_ptr<Orc::VolumeReader>& volume,
    const Orc::MFTRecord& mftRecord,
    std::wstring_view name)
{
    using namespace Orc;

    for (const auto& attributeInfo : mftRecord.GetAttributeList())
    {
        if (attributeInfo.TypeCode() != $EA)
        {
            continue;
        }

        auto ea = std::dynamic_pointer_cast<ExtendedAttribute, MftRecordAttribute>(attributeInfo.Attribute());
        if (!ea)
        {
            continue;
        }

        HRESULT hr = ea->Parse(volume);
        if (FAILED(hr))
        {
            Log::Debug("Failed to parse extended attribute [{}]", SystemError(hr));
            continue;
        }

        for (const auto& [itemName, itemData] : ea->Items())
        {
            if (itemName != name)
            {
                continue;
            }

            return itemData;
        }
    }

    return std::errc::no_such_file_or_directory;
}

// Directory can have some of those attribute but usually they don't
bool IsFailureAcceptedForDirectories(const Orc::Intentions& intention)
{
    using namespace Orc;

    switch (intention)
    {
        case Intentions::FILEINFO_FILESIZE:
        case Intentions::FILEINFO_MD5:
        case Intentions::FILEINFO_SHA1:
        case Intentions::FILEINFO_FIRST_BYTES:
        case Intentions::FILEINFO_VERSION:
        case Intentions::FILEINFO_COMPANY:
        case Intentions::FILEINFO_PRODUCT:
        case Intentions::FILEINFO_ORIGINALNAME:
        case Intentions::FILEINFO_PLATFORM:
        case Intentions::FILEINFO_TIMESTAMP:
        case Intentions::FILEINFO_SUBSYSTEM:
        case Intentions::FILEINFO_FILETYPE:
        case Intentions::FILEINFO_FILEOS:
        case Intentions::FILEINFO_SHA256:
        case Intentions::FILEINFO_PE_MD5:
        case Intentions::FILEINFO_PE_SHA1:
        case Intentions::FILEINFO_PE_SHA256:
        case Intentions::FILEINFO_SECURITY_DIRECTORY:
        case Intentions::FILEINFO_AUTHENTICODE_STATUS:
        case Intentions::FILEINFO_AUTHENTICODE_SIGNER:
        case Intentions::FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT:
        case Intentions::FILEINFO_AUTHENTICODE_CA:
        case Intentions::FILEINFO_AUTHENTICODE_CA_THUMBPRINT:
        case Intentions::FILEINFO_SSDEEP:
        case Intentions::FILEINFO_TLSH:
        case Intentions::FILEINFO_SIGNED_HASH:
        case Intentions::FILEINFO_SECURITY_DIRECTORY_SIZE:
        case Intentions::FILEINFO_SECURITY_DIRECTORY_SIGNATURE_SIZE:
        case Intentions::FILEINFO_RECORDINUSE:
            return true;
    }

    return false;
}

}  // namespace

using namespace Orc;

const WCHAR* FileInfo::g_pszExecutableFileExtensions[] = {
    L".COM", L".DLL", L".CPL", L".OCX", L".SYS", L".DRV", L".FON", L".TLB", L".EXE", L".PIF", L".APP", L".PRF", L".MSI",
    L".MSP", L".CHM", L".HLP", L".SCR", L".MDB", L".WMA", L".WMV", L".MP3", L".AX",  L".ACM", L".ANI", L".ANR", NULL};

const WCHAR* FileInfo::g_pszScriptFileExtensions[] = {
    L".BAT", L".CMD", L".VBS", L".JS",  L".WSH", L".ASP", L".ASPX",     L".HTA", L".INF", L".INS",
    L".ISP", L".JSE", L".REG", L".URL", L".VB",  L".VBE", L".VSMACROS", L".VSS", L".VST", L".VSW",
    L".WS",  L".WSC", L".WSF", L".OPS", L".PCD", L".CSH", L".KSH",      L".LNK", L".MSC", L".MST",
    L".SCF", L".SCT", L".SHB", L".SHS", L".PS1", L".PSM", L".PSD",      NULL};

const WCHAR* FileInfo::g_pszArchiveFileExtensions[] =
    {L".ZIP", L".RAR", L".CAB", L".UPX", L".TAR", L".ARC", L".LHA", L".TZ", NULL};

FileInfo::FileInfo(
    std::wstring strComputerName,
    const std::shared_ptr<VolumeReader>& pVolReader,
    Intentions DefaultIntentions,
    const std::vector<Filter>& Filters,
    LPCWSTR szFullName,
    DWORD dwLen,
    Authenticode& codeVerifyTrust)
    : m_PEInfo(*this)
    , m_hFile(INVALID_HANDLE_VALUE)
    , m_pVolReader(pVolReader)
    , m_szFullName(szFullName)
    , m_dwFullNameLen(dwLen)
    , m_DefaultIntentions(DefaultIntentions)
    , m_Filters(Filters)
    , m_codeVerifyTrust(codeVerifyTrust)
{
    if (strComputerName.empty())
    {
        SystemDetails::GetOrcComputerName(m_strComputerName);
    }
    else
    {
        m_strComputerName = strComputerName;
    }

    if (!m_Filters.empty())
        m_ColumnIntentions = static_cast<Intentions>(DefaultIntentions | GetFilterIntentions(Filters));
}

FileInfo::~FileInfo()
{
    if (m_hFile != INVALID_HANDLE_VALUE)
        CloseHandle(m_hFile);
}

HRESULT FileInfo::HandleIntentions(const Intentions& intention, ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    switch (intention)
    {
        case Intentions::FILEINFO_COMPUTERNAME:
            hr = WriteComputerName(output);
            break;

        case Intentions::FILEINFO_VOLUMEID:
            hr = WriteVolumeID(output);
            break;

        case Intentions::FILEINFO_FILENAME:
            hr = WriteFileName(output);
            break;

        case Intentions::FILEINFO_PARENTNAME:
            hr = WriteParentName(output);
            break;

        case Intentions::FILEINFO_FULLNAME:
            hr = WriteFullName(output);
            break;

        case Intentions::FILEINFO_EXTENSION:
            hr = WriteExtension(output);
            break;

        case Intentions::FILEINFO_FILESIZE:
            hr = WriteSizeInBytes(output);
            break;

        case Intentions::FILEINFO_ATTRIBUTES:
            hr = WriteAttributes(output);
            break;

        case Intentions::FILEINFO_CREATIONDATE:
            hr = WriteCreationDate(output);
            break;

        case Intentions::FILEINFO_LASTMODDATE:
            hr = WriteLastModificationDate(output);
            break;

        case Intentions::FILEINFO_LASTACCDATE:
            hr = WriteLastAccessDate(output);
            break;

        case Intentions::FILEINFO_RECORDINUSE:
            hr = WriteRecordInUse(output);
            break;

        case Intentions::FILEINFO_SHORTNAME:
            hr = WriteShortName(output);
            break;

        case Intentions::FILEINFO_MD5:
            hr = WriteMD5(output);
            break;

        case Intentions::FILEINFO_SHA1:
            hr = WriteSHA1(output);
            break;

        case Intentions::FILEINFO_FIRST_BYTES:
            hr = WriteFirstBytes(output);
            break;

        case Intentions::FILEINFO_VERSION:
            hr = WriteVersion(output);
            break;

        case Intentions::FILEINFO_COMPANY:
            hr = WriteCompanyName(output);
            break;

        case Intentions::FILEINFO_PRODUCT:
            hr = WriteProductName(output);
            break;

        case Intentions::FILEINFO_ORIGINALNAME:
            hr = WriteOriginalFileName(output);
            break;

        case Intentions::FILEINFO_PLATFORM:
            hr = WritePlatform(output);
            break;

        case Intentions::FILEINFO_TIMESTAMP:
            hr = WriteTimeStamp(output);
            break;

        case Intentions::FILEINFO_SUBSYSTEM:
            hr = WriteSubSystem(output);
            break;

        case Intentions::FILEINFO_FILETYPE:
            hr = WriteFileType(output);
            break;

        case Intentions::FILEINFO_FILEOS:
            hr = WriteFileOS(output);
            break;

        case Intentions::FILEINFO_SHA256:
            hr = WriteSHA256(output);
            break;

        case Intentions::FILEINFO_PE_MD5:
            hr = WritePeMD5(output);
            break;

        case Intentions::FILEINFO_PE_SHA1:
            hr = WritePeSHA1(output);
            break;

        case Intentions::FILEINFO_PE_SHA256:
            hr = WritePeSHA256(output);
            break;

        case Intentions::FILEINFO_SECURITY_DIRECTORY:
            hr = WriteSecurityDirectory(output);
            break;

        case Intentions::FILEINFO_AUTHENTICODE_STATUS:
            hr = WriteAuthenticodeStatus(output);
            break;

        case Intentions::FILEINFO_AUTHENTICODE_SIGNER:
            hr = WriteAuthenticodeSigner(output);
            break;

        case Intentions::FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT:
            hr = WriteAuthenticodeSignerThumbprint(output);
            break;

        case Intentions::FILEINFO_AUTHENTICODE_CA:
            hr = WriteAuthenticodeCA(output);
            break;

        case Intentions::FILEINFO_AUTHENTICODE_CA_THUMBPRINT:
            hr = WriteAuthenticodeCAThumbprint(output);
            break;

        case Intentions::FILEINFO_SSDEEP:
            hr = WriteSSDeep(output);
            break;

        case Intentions::FILEINFO_TLSH:
            hr = output.WriteNothing();
            break;

        case Intentions::FILEINFO_SIGNED_HASH:
            hr = WriteSignedHash(output);
            break;

        case Intentions::FILEINFO_SECURITY_DIRECTORY_SIZE:
            hr = WriteSecurityDirectorySize(output);
            break;

        case Intentions::FILEINFO_SECURITY_DIRECTORY_SIGNATURE_SIZE:
            hr = WriteSecurityDirectorySignatureSize(output);
            break;

        case Intentions::FILEINFO_OWNER:
        case Intentions::FILEINFO_OWNERID:
        case Intentions::FILEINFO_OWNERSID:
            return output.WriteNothing();

        default:
            return E_FAIL;
            break;
    }

    return hr;
}

HRESULT FileInfo::WriteFileInformation(
    const ColumnNameDef columnNames[],
    ITableOutput& output,
    const std::vector<Filter>& filters)
{
    HRESULT hr = E_FAIL;

    Intentions localIntentions = FilterIntentions(filters);

    const ColumnNameDef* pCurCol = columnNames;
    while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
    {
        DWORD ColId = output.GetCurrentColumnID();

        try
        {
            if (HasFlag(localIntentions, pCurCol->dwIntention))
            {
                if (FAILED(hr = HandleIntentions(pCurCol->dwIntention, output)))
                {
                    if (IsDirectory() && ::IsFailureAcceptedForDirectories(pCurCol->dwIntention))
                    {
                        if (output.GetCurrentColumnID() == ColId)
                            hr = output.WriteNothing();
                    }
                    else
                    {
                        Log::Debug(
                            L"Failed to write column '{}' for '{}' [{}]",
                            pCurCol->szColumnName,
                            m_szFullName,
                            SystemError(hr));

                        if (output.GetCurrentColumnID() == ColId)
                            hr = output.AbandonColumn();
                    }
                }
            }
            else
                hr = output.WriteNothing();
        }
        catch (Orc::Exception& e)
        {
            Log::Error(L"Error while writing column '{}': {}", output.GetCurrentColumn().ColumnName, e.Description);
            output.AbandonColumn();
        }

        _ASSERT(output.GetCurrentColumnID() == ColId + 1);

        pCurCol++;
    }
    output.WriteEndOfLine();

    return hr;
}

bool FileInfo::HasBinaryExtension(const WCHAR* pszName)
{
    return HasSpecificExtension(pszName, g_pszExecutableFileExtensions);
}

bool FileInfo::HasScriptExtension(const WCHAR* pszName)
{
    return HasSpecificExtension(pszName, g_pszScriptFileExtensions);
}

bool FileInfo::HasArchiveExtension(const WCHAR* pszName)
{
    return HasSpecificExtension(pszName, g_pszArchiveFileExtensions);
}

bool FileInfo::HasSpecificExtension(const WCHAR* pszName, const WCHAR* pszExtensions[])
{
    WCHAR ext[_MAX_EXT];

    if (_wsplitpath_s(pszName, NULL, 0L, NULL, 0L, NULL, 0L, ext, _MAX_EXT))
        return false;

    WCHAR* pCurrent = ext;

    while (*pCurrent)
    {
        *pCurrent = towupper(*pCurrent);
        pCurrent++;
    }

    const WCHAR** pCurrentExt = pszExtensions;
    while (*pCurrentExt)
    {
        if (!wcscmp(*pCurrentExt, ext))
            return true;
        pCurrentExt++;
    }
    return false;
}

Intentions
FileInfo::GetIntentions(const WCHAR* Params, const ColumnNameDef aliasNames[], const ColumnNameDef columnNames[])
{
    Intentions dwIntentions = Intentions::FILEINFO_NONE;
    WCHAR szParams[ORC_MAX_PATH];
    WCHAR* pCur = szParams;

    StringCchCopy(szParams, ORC_MAX_PATH, Params);
    StringCchCat(szParams, ORC_MAX_PATH, L",");
    DWORD dwParamLen = (DWORD)wcslen(szParams);

    for (DWORD i = 0; i < dwParamLen; i++)
    {
        if (szParams[i] == L',' || szParams[i] == L'\0')
        {
            szParams[i] = 0;
            auto bNameFound = false;

            const ColumnNameDef* pCurAlias = aliasNames;
            while (pCurAlias->dwIntention != Intentions::FILEINFO_NONE)
            {
                if (!_wcsicmp(pCur, pCurAlias->szColumnName))
                {
                    dwIntentions = static_cast<Intentions>(dwIntentions | pCurAlias->dwIntention);
                    bNameFound = true;
                    break;
                }
                pCurAlias++;
            }

            const ColumnNameDef* pCurCol = columnNames;
            while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
            {
                if (!_wcsicmp(pCur, pCurCol->szColumnName))
                {
                    dwIntentions = static_cast<Intentions>(dwIntentions | pCurCol->dwIntention);
                    bNameFound = true;
                    break;
                }
                pCurCol++;
            }
            if (!bNameFound)
            {
                Log::Warn(L"Parameter '{}' was not recognized as a valid column name", pCur);
            }
            szParams[i] = L',';
            pCur = &(szParams[i]) + 1;
        }
    }
    return dwIntentions;
}

Intentions FileInfo::GetFilterIntentions(const std::vector<Filter>& filters)
{
    Intentions intentions = Intentions::FILEINFO_NONE;

    std::for_each(filters.begin(), filters.end(), [&intentions](const Filter& item) {
        intentions = static_cast<Intentions>(intentions | item.intent);
    });
    return intentions;
}

HRESULT FileInfo::BindColumns(
    const ColumnNameDef columnNames[],
    Intentions dwIntentions,
    const std::vector<TableOutput::Column>& sqlcolumns,
    TableOutput::IConnectWriter& Writer)
{
    DWORD dwIndex = 0L;
    const ColumnNameDef* pCurCol = columnNames;

    while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
    {
        if (HasFlag(dwIntentions, pCurCol->dwIntention))
        {
            dwIndex++;

            auto it =
                std::find_if(begin(sqlcolumns), end(sqlcolumns), [pCurCol](const TableOutput::Column& aCol) -> bool {
                    return !_wcsicmp(pCurCol->szColumnName, aCol.ColumnName.c_str());
                });

            if (it != end(sqlcolumns))
                Writer.AddColumn(dwIndex, pCurCol->szColumnName, it->Type, it->dwMaxLen.value());
            else
            {
                Log::Error(
                    L"Could not find a column type for '{}' in schema definition, binding it to nothing",
                    pCurCol->szColumnName);
                Writer.AddColumn(dwIndex, pCurCol->szColumnName, TableOutput::ColumnType::Nothing, 0);
            }
        }
        else
        {
            dwIndex++;
            Writer.AddColumn(dwIndex, pCurCol->szColumnName, TableOutput::ColumnType::Nothing);
        }
        pCurCol++;
    }
    if (!dwIndex)
        return E_INVALIDARG;

    return S_OK;
}

DWORD FileInfo::GetRequiredAccessMask(const ColumnNameDef columnNames[])
{
    DWORD dwRequiredAccess = 0L;
    const ColumnNameDef* pCurCol = columnNames;
    while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
    {
        if (HasFlag(m_DefaultIntentions, pCurCol->dwIntention))
            dwRequiredAccess |= pCurCol->dwRequiredAccess;
        pCurCol++;
    }

    std::for_each(m_Filters.begin(), m_Filters.end(), [&dwRequiredAccess, &columnNames](const Filter& filter) {
        const ColumnNameDef* pCurCol = columnNames;
        while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
        {
            if (HasFlag(filter.intent, pCurCol->dwIntention))
                dwRequiredAccess |= pCurCol->dwRequiredAccess;
            pCurCol++;
        }
    });
    return dwRequiredAccess;
}

HRESULT FileInfo::CheckFileHandle()
{
    if (m_hFile != INVALID_HANDLE_VALUE)
        return S_OK;
    return Open();
}

HRESULT FileInfo::CheckStream()
{
    if (IsDirectory())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);

    if (GetDetails() == nullptr)
        return E_POINTER;

    if (GetDetails()->GetDataStream() != nullptr)
        return S_OK;

    GetDetails()->SetDataStream(GetFileStream());

    if (GetDetails()->GetDataStream() != nullptr)
        return S_OK;

    return E_FAIL;
}

HRESULT FileInfo::CheckFirstBytes()
{
    if (GetDetails() == nullptr)
        return E_POINTER;
    if (GetDetails()->FirstBytesAvailable())
        return S_OK;
    return OpenFirstBytes();
}

HRESULT FileInfo::OpenFirstBytes()
{
    HRESULT hr = E_FAIL;

    if (IsDirectory())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);

    if (FAILED(hr = CheckStream()))
        return hr;

    CBinaryBuffer FBBuffer;
    FBBuffer.SetCount(BYTES_IN_FIRSTBYTES);
    FBBuffer.ZeroMe();

    ULONGLONG ullBytesRead = 0L;

    std::shared_ptr<ByteStream> stream = GetDetails()->GetDataStream();

    if (FAILED(hr = stream->SetFilePointer(0LL, FILE_BEGIN, NULL)))
        return hr;
    if (FAILED(hr = stream->Read(FBBuffer.GetData(), BYTES_IN_FIRSTBYTES, &ullBytesRead)))
        return hr;

    FBBuffer.SetCount(ullBytesRead);
    GetDetails()->SetFirstBytes(std::move(FBBuffer));

    return S_OK;
}

HRESULT FileInfo::CheckHash()
{
    if (GetDetails() == nullptr)
        return E_POINTER;
    if (GetDetails()->HashChecked())
        return S_OK;
    return OpenHash();
}

HRESULT FileInfo::OpenHash()
{
    HRESULT hr = E_FAIL;

    const auto& details = GetDetails();

    if (details->HashChecked())
        return S_OK;

    if (details->HashAvailable() && details->HashAvailable())
        return S_OK;

    details->SetHashChecked(true);

    if (FAILED(hr = CheckStream()))
        return hr;

    Intentions localIntentions = FilterIntentions(m_Filters);

    if (HasAnyFlag(
            localIntentions,
            Intentions::FILEINFO_MD5 | Intentions::FILEINFO_SHA1 | Intentions::FILEINFO_SHA256
                | Intentions::FILEINFO_SSDEEP))
    {
        if (HasAnyFlag(
                localIntentions,
                Intentions::FILEINFO_PE_MD5 | Intentions::FILEINFO_PE_SHA1 | Intentions::FILEINFO_PE_SHA256
                    | Intentions::FILEINFO_AUTHENTICODE_STATUS | Intentions::FILEINFO_AUTHENTICODE_SIGNER))
        {
            if (FAILED(hr = m_PEInfo.CheckPEInformation()))
                return hr;
            if (m_PEInfo.HasPEHeader())
                return m_PEInfo.OpenAllHash(localIntentions);
            else
                return OpenCryptoAndFuzzyHash(localIntentions);
        }
        else
            return OpenCryptoAndFuzzyHash(localIntentions);
    }
    else if (HasAnyFlag(
                 localIntentions,
                 Intentions::FILEINFO_PE_MD5 | Intentions::FILEINFO_PE_SHA1 | Intentions::FILEINFO_PE_SHA256
                     | Intentions::FILEINFO_AUTHENTICODE_STATUS | Intentions::FILEINFO_AUTHENTICODE_SIGNER))
    {
        if (FAILED(hr = m_PEInfo.CheckPEInformation()))
            return hr;
        if (m_PEInfo.HasPEHeader())
            return m_PEInfo.OpenPeHash(localIntentions);
    }
    return S_OK;
}

HRESULT FileInfo::OpenCryptoHash(Intentions localIntentions)
{
    HRESULT hr = E_FAIL;

    if (GetDetails()->HashAvailable())
        return S_OK;

    CryptoHashStream::Algorithm algs = CryptoHashStream::Algorithm::Undefined;
    if (HasFlag(localIntentions, Intentions::FILEINFO_MD5))
        algs |= CryptoHashStream::Algorithm::MD5;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SHA1))
        algs |= CryptoHashStream::Algorithm::SHA1;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SHA256))
        algs |= CryptoHashStream::Algorithm::SHA256;

    auto stream = GetDetails()->GetDataStream();

    if (stream == nullptr)
        return E_POINTER;

    stream->SetFilePointer(0L, FILE_BEGIN, NULL);

    auto hashstream = std::make_shared<CryptoHashStream>();
    if (hashstream == nullptr)
        return E_OUTOFMEMORY;

    if (FAILED(hr = hashstream->OpenToWrite(algs, nullptr)))
        return hr;

    ULONGLONG ullWritten = 0LL;
    if (FAILED(hr = stream->CopyTo(*hashstream, &ullWritten)))
        return hr;

    if (ullWritten > 0)
    {
        if (HasFlag(algs, CryptoHashStream::Algorithm::MD5)
            && FAILED(hr = hashstream->GetHash(CryptoHashStream::Algorithm::MD5, GetDetails()->MD5())))
        {
            if (hr != MK_E_UNAVAILABLE)
                return hr;
        }
        if (HasFlag(algs, CryptoHashStream::Algorithm::SHA1)
            && FAILED(hr = hashstream->GetHash(CryptoHashStream::Algorithm::SHA1, GetDetails()->SHA1())))
        {
            if (hr != MK_E_UNAVAILABLE)
                return hr;
        }
        if (HasFlag(algs, CryptoHashStream::Algorithm::SHA256)
            && FAILED(hr = hashstream->GetHash(CryptoHashStream::Algorithm::SHA256, GetDetails()->SHA256())))
        {
            if (hr != MK_E_UNAVAILABLE)
                return hr;
        }
    }

    return S_OK;
}

HRESULT FileInfo::OpenFuzzyHash(Intentions localIntentions)
{
    HRESULT hr = E_FAIL;

    if (GetDetails()->HashAvailable())
        return S_OK;

    FuzzyHashStream::Algorithm algs = FuzzyHashStream::Algorithm::Undefined;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SSDEEP))
        algs |= FuzzyHashStream::Algorithm::SSDeep;

    auto stream = GetDetails()->GetDataStream();

    if (stream == nullptr)
        return E_POINTER;

    stream->SetFilePointer(0L, FILE_BEGIN, NULL);

    auto hashstream = std::make_shared<FuzzyHashStream>();
    if (hashstream == nullptr)
        return E_OUTOFMEMORY;

    if (FAILED(hr = hashstream->OpenToWrite(algs, nullptr)))
        return hr;

    ULONGLONG ullWritten = 0LL;
    if (FAILED(hr = stream->CopyTo(*hashstream, &ullWritten)))
        return hr;

    if (ullWritten > 0)
    {
        if (HasFlag(algs, FuzzyHashStream::Algorithm::SSDeep))
        {
            hr = hashstream->GetHash(FuzzyHashStream::Algorithm::SSDeep, GetDetails()->SSDeep());
            if (FAILED(hr) && hr != MK_E_UNAVAILABLE)
            {
                return hr;
            }
        }
    }

    return S_OK;
}

HRESULT FileInfo::OpenCryptoAndFuzzyHash(Intentions localIntentions)
{
    HRESULT hr = E_FAIL;

    if (GetDetails()->HashAvailable())
        return S_OK;

    CryptoHashStream::Algorithm crypto_algs = CryptoHashStream::Algorithm::Undefined;
    if (HasFlag(localIntentions, Intentions::FILEINFO_MD5))
        crypto_algs |= CryptoHashStream::Algorithm::MD5;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SHA1))
        crypto_algs |= CryptoHashStream::Algorithm::SHA1;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SHA256))
        crypto_algs |= CryptoHashStream::Algorithm::SHA256;

    FuzzyHashStream::Algorithm fuzzy_algs = FuzzyHashStream::Algorithm::Undefined;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SSDEEP))
        fuzzy_algs |= FuzzyHashStream::Algorithm::SSDeep;

    auto stream = GetDetails()->GetDataStream();

    if (stream == nullptr)
        return E_POINTER;

    stream->SetFilePointer(0L, FILE_BEGIN, NULL);

    std::shared_ptr<ByteStream> hashing_stream;
    std::shared_ptr<CryptoHashStream> crypto_hashstream;
    std::shared_ptr<FuzzyHashStream> fuzzy_hashstream;

    if (crypto_algs != CryptoHashStream::Algorithm::Undefined)
    {
        crypto_hashstream = std::make_shared<CryptoHashStream>();
        if (crypto_hashstream == nullptr)
            return E_OUTOFMEMORY;

        if (FAILED(hr = crypto_hashstream->OpenToWrite(crypto_algs, nullptr)))
            return hr;

        hashing_stream = crypto_hashstream;
    }

    if (fuzzy_algs != FuzzyHashStream::Algorithm::Undefined)
    {
        fuzzy_hashstream = std::make_shared<FuzzyHashStream>();
        if (fuzzy_hashstream == nullptr)
            return E_OUTOFMEMORY;

        if (FAILED(hr = fuzzy_hashstream->OpenToWrite(fuzzy_algs, hashing_stream)))
            return hr;

        hashing_stream = fuzzy_hashstream;
    }

    ULONGLONG ullWritten = 0LL;
    if (FAILED(hr = stream->CopyTo(*hashing_stream, &ullWritten)))
        return hr;

    if (ullWritten > 0)
    {
        if (HasFlag(crypto_algs, CryptoHashStream::Algorithm::MD5)
            && FAILED(hr = crypto_hashstream->GetHash(CryptoHashStream::Algorithm::MD5, GetDetails()->MD5())))
        {
            if (hr != MK_E_UNAVAILABLE)
                return hr;
        }
        if (HasFlag(crypto_algs, CryptoHashStream::Algorithm::SHA1)
            && FAILED(hr = crypto_hashstream->GetHash(CryptoHashStream::Algorithm::SHA1, GetDetails()->SHA1())))
        {
            if (hr != MK_E_UNAVAILABLE)
                return hr;
        }
        if (HasFlag(crypto_algs, CryptoHashStream::Algorithm::SHA256)
            && FAILED(hr = crypto_hashstream->GetHash(CryptoHashStream::Algorithm::SHA256, GetDetails()->SHA256())))
        {
            if (hr != MK_E_UNAVAILABLE)
                return hr;
        }
#ifdef ORC_BUILD_SSDEEP
        if (HasFlag(fuzzy_algs, FuzzyHashStream::Algorithm::SSDeep)
            && FAILED(hr = fuzzy_hashstream->GetHash(FuzzyHashStream::Algorithm::SSDeep, GetDetails()->SSDeep())))
        {
            if (hr != MK_E_UNAVAILABLE)
                return hr;
        }
#endif
    }

    return S_OK;
}

HRESULT FileInfo::CheckAuthenticodeData()
{
    if (GetDetails() == nullptr)
        return E_POINTER;
    if (IsDirectory())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    if (GetDetails()->HasAuthenticodeData())
        return S_OK;
    return OpenAuthenticode();
}

HRESULT FileInfo::OpenAuthenticode()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckHash()))
        return hr;

    Authenticode::AuthenticodeData data(m_codeVerifyTrust.Cache());
    if (IsDirectory() || !m_PEInfo.HasPEHeader())
    {
        GetDetails()->SetAuthenticodeData(std::move(data));  // Setting empty Authenticode data
        return S_OK;
    }

    if (FAILED(hr = m_PEInfo.CheckSecurityDirectory()))
        return hr;

    if (GetDetails()->SecurityDirectoryAvailable())
    {
        if (FAILED(
                hr = m_codeVerifyTrust.Verify(
                    m_szFullName, GetDetails()->SecurityDirectory(), GetDetails()->GetPEHashs(), data)))
        {
            Log::Warn(L"WinVerifyTrust failed for file '{}' [{}]", m_szFullName, SystemError(hr));
        }
    }
    else
    {
        hr = VerifyAnySignatureWithCatalogs(m_szFullName, GetDetails()->GetPEHashs(), data);
        if (FAILED(hr))
        {
            Log::Warn(L"WinVerifyTrust failed for file '{}' [{}]", m_szFullName, SystemError(hr));
        }
    }
    GetDetails()->SetAuthenticodeData(std::move(data));
    return S_OK;
}

HRESULT FileInfo::VerifyAnySignatureWithCatalogs(
    const std::wstring_view path,
    const Authenticode::PE_Hashs& peHashes,
    Authenticode::AuthenticodeData& data)
{
    HRESULT hr = m_codeVerifyTrust.VerifyAnySignatureWithCatalogs(m_szFullName, peHashes, data);
    if (SUCCEEDED(hr))
    {
        return S_OK;
    }

    Log::Debug(L"Failed to verify signature with WinVerifyTrust [{}]", hr);

    auto mftRecordInfo = dynamic_cast<MFTRecordFileInfo*>(this);
    if (!mftRecordInfo || mftRecordInfo->MftRecord() == nullptr)
    {
        return E_FAIL;
    }

    auto catalogHint = ::ReadExtendedAttribute(m_pVolReader, *mftRecordInfo->MftRecord(), L"$CI.CATALOGHINT");
    if (!catalogHint)
    {
        return E_FAIL;
    }

    auto rv = Authenticode::VerifySignatureWithCatalogHint(*catalogHint, peHashes, data);
    if (rv.has_error())
    {
        Log::Debug(L"Failed to verify signature with CI.CATALOGHINT [{}]", rv.error());
        return ToHRESULT(rv.error());
    }

    return S_OK;
}

HRESULT FileInfo::WriteComputerName(ITableOutput& output)
{
    if (!m_strComputerName.empty())
        return output.WriteString(m_strComputerName);
    else
        return SystemDetails::WriteOrcComputerName(output);
}

HRESULT FileInfo::WriteVolumeID(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = output.WriteInteger(m_pVolReader->VolumeSerialNumber())))
        return hr;

    return S_OK;
}

HRESULT FileInfo::WriteFullName(ITableOutput& output)
{
    return output.WriteString(m_szFullName);
}

HRESULT FileInfo::WriteFileName(ITableOutput& output)
{
    WCHAR fname[_MAX_FNAME];
    WCHAR ext[_MAX_EXT];
    errno_t err = 0;

    if ((err = _wsplitpath_s(m_szFullName, NULL, 0, NULL, 0, fname, _MAX_FNAME, ext, _MAX_EXT)) != 0)
        return HRESULT_FROM_WIN32(err);

    return output.WriteFormated(L"{}{}", fname, ext);
}

HRESULT FileInfo::WriteParentName(ITableOutput& output)
{
    WCHAR drive[_MAX_DRIVE];
    WCHAR dir[_MAX_DIR];
    errno_t err;

    if ((err = _wsplitpath_s(m_szFullName, drive, _MAX_DRIVE, dir, _MAX_DIR, NULL, 0L, NULL, 0L)) != 0)
        return HRESULT_FROM_WIN32(err);

    // Internal [76]: avoid path ending with '\\' for weak csv parsers
    const auto dirLength = wcslen(dir);
    if (dirLength && dir[dirLength - 1] == L'\\')
    {
        dir[dirLength - 1] = L'\0';
    }

    return output.WriteFormated(L"{}{}", drive, dir);
}

HRESULT FileInfo::WriteExtension(ITableOutput& output)
{
    WCHAR ext[_MAX_EXT];
    errno_t err;
    if ((err = _wsplitpath_s(m_szFullName, NULL, 0L, NULL, 0L, NULL, 0L, ext, _MAX_EXT)) != 0)
    {
        return HRESULT_FROM_WIN32(err);
    }

    return output.WriteFormated(L"{}", ext);
}

HRESULT FileInfo::WriteOwnerId(ITableOutput& output)
{
    return output.WriteInteger((DWORD)0L);
}

HRESULT FileInfo::WriteOwnerSid(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckFileHandle()))
        return hr;

    PSID pSidOwner = NULL;
    // Allocate memory for the security descriptor structure.
    PSECURITY_DESCRIPTOR pSD = NULL;

    DWORD dwStatus =
        GetSecurityInfo(m_hFile, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &pSidOwner, NULL, NULL, NULL, &pSD);
    if (ERROR_SUCCESS != dwStatus)
        return HRESULT_FROM_WIN32(dwStatus);

    BOOST_SCOPE_EXIT(&pSD)
    {
        ::LocalFree(pSD);
    }
    BOOST_SCOPE_EXIT_END

    WCHAR* StringSid = NULL;
    if (!ConvertSidToStringSid(pSidOwner, &StringSid))
        return HRESULT_FROM_WIN32(GetLastError());
    else
        return output.WriteString(StringSid);
}

HRESULT FileInfo::WriteOwner(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckFileHandle()))
        return hr;

    PSID pSidOwner = NULL;
    // Allocate memory for the security descriptor structure.
    PSECURITY_DESCRIPTOR pSD = NULL;

    DWORD dwStatus =
        GetSecurityInfo(m_hFile, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &pSidOwner, NULL, NULL, NULL, &pSD);
    if (ERROR_SUCCESS != dwStatus)
        return HRESULT_FROM_WIN32(dwStatus);

    BOOST_SCOPE_EXIT(&pSD)
    {
        ::LocalFree(pSD);
    }
    BOOST_SCOPE_EXIT_END

#define MAX_NAME 512
    DWORD dwNameLen = MAX_NAME;
    WCHAR lpName[MAX_NAME];
    DWORD dwDomainLen = MAX_NAME;
    WCHAR lpDomain[MAX_NAME];
    SID_NAME_USE NameUse;

    if (!LookupAccountSid(NULL, pSidOwner, lpName, &dwNameLen, lpDomain, &dwDomainLen, &NameUse))
    {
        DWORD err = GetLastError();
        if (err == ERROR_NONE_MAPPED)
        {
            WCHAR* StringSid = NULL;
            if (!ConvertSidToStringSid(pSidOwner, &StringSid))
                return HRESULT_FROM_WIN32(err);
            else
                return output.WriteString(StringSid);
        }
        else
            return HRESULT_FROM_WIN32(GetLastError());
    }
    else
        return output.WriteFormated(L"{}\\{}", lpDomain, lpName);
}

HRESULT FileInfo::WritePlatform(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_PEInfo.CheckPEInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }

    if (m_PEInfo.HasPEHeader())
    {
        PIMAGE_NT_HEADERS32 pNTHeader = m_PEInfo.GetPe32Header();

        static const FlagsDefinition MachineDefs[] = {
            {IMAGE_FILE_MACHINE_UNKNOWN, L"MachineUnknown", L"MachineUnknown"},
            {IMAGE_FILE_MACHINE_TARGET_HOST, L"Target Host", L"Interacts with the host and not a WOW64 guest"},
            {IMAGE_FILE_MACHINE_ARM64, L"ARM64", L"ARM64 Machine"},
            {IMAGE_FILE_MACHINE_ALPHA64, L"ALPHA64", L"ALPHA64 Machine"},
            {IMAGE_FILE_MACHINE_I386, L"x86", L"X86 Machine"},
            {IMAGE_FILE_MACHINE_IA64, L"IA64", L"Titanium Machine"},
            {IMAGE_FILE_MACHINE_AMD64, L"x64", L"Machine"},
            {IMAGE_FILE_MACHINE_EBC, L"EFI", L"Machine"},
            {IMAGE_FILE_MACHINE_R3000, L"MIPS little-endian", L"MIPS little-endian Machine"},
            {IMAGE_FILE_MACHINE_R4000, L"MIPS little-endian", L"MIPS little-endian Machine"},
            {IMAGE_FILE_MACHINE_R10000, L"MIPS little-endian", L"MIPS little-endian Machine"},
            {IMAGE_FILE_MACHINE_WCEMIPSV2, L"MIPS little-endian WCE v2", L"MIPS little-endian WCE v2 Machine"},
            {IMAGE_FILE_MACHINE_ALPHA, L"ALPHA64", L"ALPHA64 Machine"},
            {IMAGE_FILE_MACHINE_AXP64, L"Alpha_AXP", L"Alpha_AXP Machine"},
            {IMAGE_FILE_MACHINE_SH3, L"SH3 little-endian", L"SH3 little-endian Machine"},
            {IMAGE_FILE_MACHINE_SH3DSP, L"SH3", L"SH3 Machine"},
            {IMAGE_FILE_MACHINE_SH3E, L"SH3E little-endian", L"SH3E little-endian Machine"},
            {IMAGE_FILE_MACHINE_SH4, L"SH4 little-endian", L"SH4 little-endian Machine"},
            {IMAGE_FILE_MACHINE_SH5, L"SH5", L"SH5 Machine"},
            {IMAGE_FILE_MACHINE_ARM, L"ARM Little-Endian", L"ARM Little-Endian Machine"},
            {IMAGE_FILE_MACHINE_ARMNT, L"ARM Thumb-2 Little-Endian", L"ARM Thumb-2 Little-Endian Machine"},
            {IMAGE_FILE_MACHINE_THUMB, L"THUMB", L"THUMB Machine"},
            {IMAGE_FILE_MACHINE_AM33, L"AM33", L"AM33 Machine"},
            {IMAGE_FILE_MACHINE_POWERPC, L"IBM PowerPC Little-Endian", L"IBM PowerPC Little-Endian Machine"},
            {IMAGE_FILE_MACHINE_POWERPCFP, L"IBM PowerPC", L"IBM PowerPC Machine"},
            {IMAGE_FILE_MACHINE_MIPS16, L"MIPS", L"MIPS Machine"},
            {IMAGE_FILE_MACHINE_MIPSFPU, L"MIPS", L"MIPSMachine"},
            {IMAGE_FILE_MACHINE_MIPSFPU16, L"MIPS", L"MIPS Machine"},
            {IMAGE_FILE_MACHINE_TRICORE, L"Infineon", L"Infineon Machine"},
            {IMAGE_FILE_MACHINE_CEF, L"CEF", L"CEF Machine"},
            {IMAGE_FILE_MACHINE_M32R, L"M32R little-endian", L"M32R little-endian Machine"},
            {IMAGE_FILE_MACHINE_CEE, L"CEE", L"CEE Machine"},
            {IMAGE_FILE_MACHINE_OSX, L"OSX", L"OSX Machine [undocumented]"},
            {IMAGE_FILE_MACHINE_LINUX, L"Linux", L"Linux Machine [undocumented]"},
            {(DWORD)-1, NULL, NULL}};

        // @@Platform
        if (pNTHeader != nullptr)
            return output.WriteExactFlags(pNTHeader->FileHeader.Machine, MachineDefs);
        else
            return output.WriteNothing();
    }
    else if (m_PEInfo.HasExeHeader())
    {
        return output.WriteString(L"DOS");
    }

    return output.WriteNothing();
}

HRESULT FileInfo::WriteTimeStamp(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_PEInfo.CheckPEInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }

    if (m_PEInfo.HasPEHeader())
    {
        // same offset pe32/pe64
        PIMAGE_NT_HEADERS32 pPE = m_PEInfo.GetPe32Header();

        if (pPE != nullptr)
            return output.WriteTimeStamp(pPE->FileHeader.TimeDateStamp);
        else
            return output.WriteNothing();
    }
    return output.WriteNothing();
}

HRESULT FileInfo::WriteSubSystem(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_PEInfo.CheckPEInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }

    if (m_PEInfo.HasPEHeader())
    {
        static const FlagsDefinition SubSystemDefs[] = {
            {IMAGE_SUBSYSTEM_UNKNOWN, L"Unknown", L"Unknown"},
            {IMAGE_SUBSYSTEM_NATIVE, L"Native", L"Native"},
            {IMAGE_SUBSYSTEM_WINDOWS_GUI, L"Windows GUI", L"Windows GUI"},
            {IMAGE_SUBSYSTEM_WINDOWS_CUI, L"Windows Console", L"Windows Console"},
            {IMAGE_SUBSYSTEM_OS2_CUI, L"OS/2 Console", L"OS/2 Console"},
            {IMAGE_SUBSYSTEM_POSIX_CUI, L"Posix Console", L"Posix Console"},
            {IMAGE_SUBSYSTEM_NATIVE_WINDOWS, L"Windows 9x", L"Windows 9x"},
            {IMAGE_SUBSYSTEM_WINDOWS_CE_GUI, L"Windows CE", L"Windows CE"},
            {IMAGE_SUBSYSTEM_EFI_APPLICATION, L"EFI App", L"EFI App"},
            {IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER, L"EFI Boot", L"EFI Boot"},
            {IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER, L"EFI Runtime", L"EFI Runtime"},
            {IMAGE_SUBSYSTEM_EFI_ROM, L"EFI ROM", L"EFI ROM"},
            {IMAGE_SUBSYSTEM_XBOX, L"XBOX App", L"XBOX App"},
            {IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION, L"Windows Boot", L"Windows Boot"},
            {(DWORD)-1, NULL, NULL}};
        // same offset pe32/pe64
        PIMAGE_NT_HEADERS32 pPE = m_PEInfo.GetPe32Header();
        if (pPE != nullptr)
            return output.WriteExactFlags(pPE->OptionalHeader.Subsystem, SubSystemDefs);
        else
            return output.WriteNothing();
    }

    return output.WriteNothing();
}

HRESULT FileInfo::WriteFileOS(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_PEInfo.CheckVersionInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }

    if (GetDetails()->FileVersionAvailable())
    {
        static const FlagsDefinition SubOSDefs[] = {
            {VOS__BASE, L"Unknown", L"Unknown"},
            {VOS__WINDOWS16, L"WINDOWS16", L"WINDOWS16"},
            {VOS__PM16, L"PM16", L"PM16"},
            {VOS__PM32, L"PM32", L"PM32"},
            {VOS__WINDOWS32, L"WINDOWS32", L"WINDOWS32"},
            {VOS_DOS, L"DOS", L"DOS"},
            {VOS_OS216, L"OS/2-16", L"OS/2-16"},
            {VOS_OS232, L"OS/2-32", L"OS/2-32"},
            {VOS_NT, L"NT", L"NT"},
            {VOS_WINCE, L"CE", L"CE"},
            {VOS_DOS_WINDOWS16, L"DOS-Win16", L"DOS-Win16"},
            {VOS_DOS_WINDOWS32, L"DOS-Win32", L"DOS-Win32"},
            {VOS_OS216_PM16, L"OS/2-16_PM-16", L"OS/2-16_PM-16"},
            {VOS_OS232_PM32, L"OS/2-32_PM-32", L"OS/2-32_PM-32"},
            {VOS_NT_WINDOWS32, L"NT-Win32", L"NT-Win32"},
            {(DWORD)-1, NULL, NULL}};

        VS_FIXEDFILEINFO* pFFI = GetDetails()->GetFixedFileInfo();
        return output.WriteExactFlags(pFFI->dwFileOS, SubOSDefs);
    }
    return output.WriteNothing();
}

HRESULT FileInfo::WriteFileType(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_PEInfo.CheckVersionInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }

    if (GetDetails()->FileVersionAvailable())
    {
        VS_FIXEDFILEINFO* m_pFFI = GetDetails()->GetFixedFileInfo();
        switch (m_pFFI->dwFileType)
        {
            case VFT_UNKNOWN:
                return output.WriteString(L"Unknown");
            case VFT_APP:
                return output.WriteString(L"Application");
            case VFT_DLL:
                return output.WriteString(L"DLL");
            case VFT_DRV: {
                switch (m_pFFI->dwFileSubtype)
                {
                    case VFT2_UNKNOWN:
                        return output.WriteString(L"Driver Unknown");
                    case VFT2_DRV_PRINTER:
                        return output.WriteString(L"Driver Printer");
                    case VFT2_DRV_KEYBOARD:
                        return output.WriteString(L"Driver Keyboard");
                    case VFT2_DRV_LANGUAGE:
                        return output.WriteString(L"Driver Language");
                    case VFT2_DRV_DISPLAY:
                        return output.WriteString(L"Driver Display");
                    case VFT2_DRV_MOUSE:
                        return output.WriteString(L"Driver Mouse");
                    case VFT2_DRV_NETWORK:
                        return output.WriteString(L"Driver Network");
                    case VFT2_DRV_SYSTEM:
                        return output.WriteString(L"Driver System");
                    case VFT2_DRV_INSTALLABLE:
                        return output.WriteString(L"Driver Installable");
                    case VFT2_DRV_SOUND:
                        return output.WriteString(L"Driver Sound");
                    case VFT2_DRV_COMM:
                        return output.WriteString(L"Driver Com");
                    case VFT2_DRV_INPUTMETHOD:
                        return output.WriteString(L"Driver Input Method");
                    case VFT2_DRV_VERSIONED_PRINTER:
                        return output.WriteString(L"Driver Printer Versioned");
                }
                return output.WriteFormated(L"Driver {:#x}-{:#x}", m_pFFI->dwFileType, m_pFFI->dwFileSubtype);
            }
            case VFT_FONT: {
                switch (m_pFFI->dwFileSubtype)
                {
                    case VFT2_FONT_RASTER:
                        return output.WriteString(L"Font Raster");
                    case VFT2_FONT_VECTOR:
                        return output.WriteString(L"Font Vector");
                    case VFT2_FONT_TRUETYPE:
                        return output.WriteString(L"Font TrueType");
                }
                return output.WriteFormated(L"Font {:#x}-{:#x}", m_pFFI->dwFileType, m_pFFI->dwFileSubtype);
            }
            case VFT_VXD:
                return output.WriteString(L"VXD");
            case VFT_STATIC_LIB:
                return output.WriteString(L"Lib");
        }
        return output.WriteFormated(L"{:#x}-{:#x}", m_pFFI->dwFileType, m_pFFI->dwFileSubtype);
    }
    return output.WriteNothing();
}

HRESULT FileInfo::WriteVersion(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_PEInfo.CheckVersionInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }

    if (GetDetails()->FileVersionAvailable())
    {
        VS_FIXEDFILEINFO* m_pFFI = GetDetails()->GetFixedFileInfo();
        return output.WriteFormated(
            L"{}.{}.{}.{}",
            HIWORD(m_pFFI->dwFileVersionMS),
            LOWORD(m_pFFI->dwFileVersionMS),
            HIWORD(m_pFFI->dwFileVersionLS),
            LOWORD(m_pFFI->dwFileVersionLS));
    }
    return output.WriteNothing();
}

HRESULT FileInfo::WriteCompanyName(ITableOutput& output)
{
    return WriteVersionQueryValue(L"CompanyName", output);
}

HRESULT FileInfo::WriteProductName(ITableOutput& output)
{
    return WriteVersionQueryValue(L"ProductName", output);
}

HRESULT FileInfo::WriteOriginalFileName(ITableOutput& output)
{
    return WriteVersionQueryValue(L"OriginalFilename", output);
}

HRESULT FileInfo::WritePeMD5(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }
    return GetDetails()->PeMD5().GetCount() > 0 ? output.WriteBytes(GetDetails()->PeMD5()) : output.WriteNothing();
}

HRESULT FileInfo::WritePeSHA1(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }
    return GetDetails()->PeSHA1().GetCount() > 0 ? output.WriteBytes(GetDetails()->PeSHA1()) : output.WriteNothing();
}

HRESULT FileInfo::WritePeSHA256(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }
    return GetDetails()->PeSHA256().GetCount() > 0 ? output.WriteBytes(GetDetails()->PeSHA256())
                                                   : output.WriteNothing();
}

HRESULT FileInfo::WriteMD5(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION) || hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }
    return GetDetails()->MD5().GetCount() > 0 ? output.WriteBytes(GetDetails()->MD5()) : output.WriteNothing();
}

HRESULT FileInfo::WriteSHA1(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION) || hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }

    return GetDetails()->SHA1().GetCount() > 0 ? output.WriteBytes(GetDetails()->SHA1()) : output.WriteNothing();
}

HRESULT FileInfo::WriteSHA256(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION) || hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }
    return GetDetails()->SHA256().GetCount() > 0 ? output.WriteBytes(GetDetails()->SHA256()) : output.WriteNothing();
}

HRESULT FileInfo::WriteSSDeep(ITableOutput& output)
{
#ifdef ORC_BUILD_SSDEEP
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION) || hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }
    return output.WriteString(GetDetails()->SSDeep());
#else
    return output.WriteNothing();
#endif
}

HRESULT FileInfo::WriteSignedHash(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckHash()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION) || hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }
    const auto& data = GetDetails()->GetAuthenticodeData();
    if (!data.SignedHashs.sha256.empty())
        return output.WriteBytes(data.SignedHashs.sha256);
    if (!data.SignedHashs.sha1.empty())
        return output.WriteBytes(data.SignedHashs.sha1);
    if (!data.SignedHashs.md5.empty())
        return output.WriteBytes(data.SignedHashs.md5);
    return output.WriteNothing();
}

HRESULT FileInfo::WriteSecurityDirectory(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_PEInfo.CheckSecurityDirectory()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }

    if (GetDetails()->SecurityDirectory().GetCount())
    {
        DWORD cchString = 0L;

        CryptBinaryToStringW(
            GetDetails()->SecurityDirectory().GetData(),
            (DWORD)GetDetails()->SecurityDirectory().GetCount(),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            NULL,
            &cchString);

        LPWSTR szWstr = (LPWSTR)::malloc(msl::utilities::SafeInt<DWORD>(cchString) * sizeof(WCHAR));
        if (szWstr == nullptr)
            return E_OUTOFMEMORY;

        BOOST_SCOPE_EXIT(&szWstr)
        {
            ::free(szWstr);
        }
        BOOST_SCOPE_EXIT_END

        CryptBinaryToStringW(
            GetDetails()->SecurityDirectory().GetData(),
            (DWORD)GetDetails()->SecurityDirectory().GetCount(),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            szWstr,
            &cchString);

        return output.WriteString(szWstr);
    }
    return output.WriteNothing();
}

HRESULT FileInfo::WriteSecurityDirectorySize(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_PEInfo.CheckSecurityDirectory()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }

    if (IsDirectory() || !m_PEInfo.HasPEHeader())
        return output.WriteNothing();

    if (GetDetails()->SecurityDirectory().GetCount())
        return output.WriteInteger((DWORD)GetDetails()->SecurityDirectory().GetCount());

    return output.WriteNothing();
}

HRESULT FileInfo::WriteSecurityDirectorySignatureSize(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_PEInfo.CheckSecurityDirectory()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }

    if (IsDirectory() || !m_PEInfo.HasPEHeader())
    {
        return output.WriteNothing();
    }

    if (GetDetails()->SecurityDirectoryAvailable())
    {
        DWORD cbSize = 0L;
        if (FAILED(hr = m_codeVerifyTrust.SignatureSize(m_szFullName, GetDetails()->SecurityDirectory(), cbSize)))
        {
            Log::Warn(L"SignatureSize failed for file '{}' [{}]", m_szFullName, SystemError(hr));
        }
        return output.WriteInteger(cbSize);
    }
    return output.WriteNothing();
}

HRESULT FileInfo::WriteFirstBytes(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(CheckFirstBytes()))
        return hr;

    if (FAILED(hr = output.WriteBytes(GetDetails()->FirstBytes())))
        return hr;

    return S_OK;
}

HRESULT FileInfo::WriteAuthenticodeStatus(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (!m_PEInfo.HasPEHeader())
    {
        return output.WriteExactFlags(Authenticode::AUTHENTICODE_NOT_PE, Authenticode::AuthenticodeStatusDefs);
    }

    if (FAILED(hr = CheckAuthenticodeData()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
            return output.WriteNothing();
        return hr;
    }

    return output.WriteExactFlags(GetDetails()->GetAuthenticodeData().AuthStatus, Authenticode::AuthenticodeStatusDefs);
}

HRESULT FileInfo::WriteAuthenticodeSigner(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckAuthenticodeData()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
        {
            return output.WriteNothing();
        }

        return hr;
    }

    const auto& signers = GetDetails()->GetAuthenticodeData().SignersName();

    if (signers.empty())
    {
        return output.WriteNothing();
    }

    bool bIsFirst = true;
    std::wstringstream signerstream;
    for (const auto& name : signers)
    {
        if (!bIsFirst)
        {
            signerstream << L";" << name;
        }
        else
        {
            signerstream << name;
            bIsFirst = false;
        }
    }

    return output.WriteString(signerstream.str());
}

HRESULT FileInfo::WriteAuthenticodeSignerThumbprint(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckAuthenticodeData()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
        {
            return output.WriteNothing();
        }

        return hr;
    }

    const auto& thumbprints = GetDetails()->GetAuthenticodeData().SignersThumbprint();

    if (thumbprints.empty())
    {
        return output.WriteNothing();
    }

    bool bIsFirst = true;
    std::wstringstream signerstream;
    for (const auto& thumbprint : thumbprints)
    {
        if (!bIsFirst)
        {
            signerstream << L";";
        }

        for (UINT i = 0; i < thumbprint.size(); i++)
        {
            signerstream << std::setfill(L'0') << std::setw(2) << std::hex << thumbprint[i];
        }

        bIsFirst = false;
    }

    return output.WriteString(signerstream.str());
}

HRESULT FileInfo::WriteAuthenticodeCA(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckAuthenticodeData()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
        {
            return output.WriteNothing();
        }

        return hr;
    }

    const auto& signersCAs = GetDetails()->GetAuthenticodeData().SignersCertificateAuthoritiesName();

    if (signersCAs.empty())
    {
        return output.WriteNothing();
    }

    bool bIsFirst = true;
    std::wstringstream castream;
    for (const auto& name : signersCAs)
    {
        if (!bIsFirst)
        {
            castream << L";" << name;
        }
        else
        {
            castream << name;
            bIsFirst = false;
        }
    }

    return output.WriteString(castream.str());
}

HRESULT FileInfo::WriteAuthenticodeCAThumbprint(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckAuthenticodeData()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_NO_DATA))
        {
            return output.WriteNothing();
        }

        return hr;
    }

    const auto& thumbprints = GetDetails()->GetAuthenticodeData().SignersCertificateAuthoritiesThumbprint();
    if (thumbprints.empty())
    {
        return output.WriteNothing();
    }

    bool bIsFirst = true;
    std::wstringstream castream;
    for (auto thumbprint : thumbprints)
    {
        if (!bIsFirst)
        {
            castream << L";";
        }

        for (UINT i = 0; i < thumbprint.size(); i++)
        {
            castream << std::setfill(L'0') << std::setw(2) << std::hex << thumbprint[i];
        }

        bIsFirst = false;
    }

    return output.WriteString(castream.str());
}

bool FileInfo::FilterApplies(const Filter& filter)
{
    switch (filter.type)
    {
        case FILEFILTER_EXTBINARY:
            return HasBinaryExtension(m_szFullName);
        case FILEFILTER_EXTSCRIPT:
            return HasScriptExtension(m_szFullName);
        case FILEFILTER_EXTARCHIVE:
            return HasArchiveExtension(m_szFullName);
        case FILEFILTER_EXTCUSTOM:
            return HasSpecificExtension(m_szFullName, (const WCHAR**)filter.filterdata.extcustom);
        case FILEFILTER_SIZELESS:
            return !ExceedsFileThreshold(filter.filterdata.size.HighPart, filter.filterdata.size.LowPart);
        case FILEFILTER_SIZEMORE:
            return ExceedsFileThreshold(filter.filterdata.size.HighPart, filter.filterdata.size.LowPart);
        case FILEFILTER_VERSIONINFO:
            return m_PEInfo.HasFileVersionInfo();
        case FILEFILTER_PEHEADER:
            return m_PEInfo.HasPEHeader();
    }
    return false;
}

Intentions FileInfo::FilterIntentions(const std::vector<Filter>& filters)
{
    Intentions intentions = m_DefaultIntentions;

    std::for_each(filters.begin(), filters.end(), [this, &intentions](const Filter& item) {
        if (FilterApplies(item))
        {
            if (item.bInclude)
                intentions = static_cast<Intentions>(intentions | item.intent);
            else
                intentions = static_cast<Intentions>(intentions & (Intentions)~item.intent);
        }
    });
    return intentions;
}

size_t FileInfo::FindVersionQueryValueRec(
    const WCHAR* szValueName,
    size_t dwValueCchLength,
    LPBYTE ptr,
    LPBYTE ptr_end,
    int state,
    WCHAR** ppValue)
{
    typedef struct _VERSION_CHUNK_HEADER
    {
        uint16_t len;
        uint16_t vlen;
        uint16_t type;
    } VERSION_CHUNK_HEADER, *PVERSION_CHUNK_HEADER;

    PVERSION_CHUNK_HEADER pHdr;
    size_t ptr_off = 0;

    if (ptr + ptr_off + sizeof(VERSION_CHUNK_HEADER) > ptr_end)
        return static_cast<size_t>(-1);

    pHdr = (PVERSION_CHUNK_HEADER)(ptr + ptr_off);
    ptr_off += sizeof(VERSION_CHUNK_HEADER);

    WCHAR* pChunkName = (WCHAR*)(ptr + ptr_off);
    size_t dwChunkNameCchLength = 0;

    if (FAILED(StringCchLengthW(pChunkName, ptr_end - (ptr + ptr_off), &dwChunkNameCchLength)))
        return static_cast<size_t>(-1);

    ptr_off += 2 * dwChunkNameCchLength + 2;
    ptr_off = (ptr_off + 3) & -4;  // align to %4

    int nstate = -1;

    switch (state)
    {
        case 0:
            // VS_VERSIONINFO
            ptr_off += pHdr->vlen;
            nstate = 1;
            break;

        case 1:
            if ((dwChunkNameCchLength == 14) && (!memcmp(L"StringFileInfo", pChunkName, 28)))
                nstate = 2;
            else if ((dwChunkNameCchLength == 11) && (!memcmp(L"VarFileInfo", pChunkName, 24)))
                nstate = 4;
            break;

        case 2:
            // string table
            nstate = 3;
            break;

        case 3:
            // a string: check against target
            if ((dwValueCchLength == dwChunkNameCchLength)
                && (!memcmp(szValueName, pChunkName, dwValueCchLength * 2 + 2)))
            {
                size_t value_len;
                if (FAILED(StringCchLengthW((WCHAR*)(ptr + ptr_off), ptr_end - (ptr + ptr_off), &value_len))
                    || (ptr_off + 2 * value_len > pHdr->len))
                {
                    // not 0-terminated
                    size_t hlen = pHdr->len & 0xfffffffe;
                    if (ptr_off > hlen - 2)
                        return static_cast<size_t>(-1);
                    *(WCHAR*)(ptr + hlen - 2) = 0;
                }
                *ppValue = (WCHAR*)(ptr + ptr_off);
            }

            ptr_off += 2 * pHdr->vlen;
            break;

        case 4:
            // var (translations, etc)
            ptr_off += pHdr->vlen;
            break;
    }

    if (nstate == -1)
        ptr_off = pHdr->len;

    while (1)
    {
        ptr_off = (ptr_off + 3) & -4;  // align to %4
        if (ptr_off >= pHdr->len)
            break;

        size_t sub_off =
            FindVersionQueryValueRec(szValueName, dwValueCchLength, ptr + ptr_off, ptr_end, nstate, ppValue);
        if (sub_off == (size_t)-1)
            return sub_off;

        ptr_off += sub_off;

        if ((sub_off == 0) && (ptr_off == ((ptr_off + 3) & -4)))
            return (size_t)-1;
    }

    return ptr_off;
}

HRESULT FileInfo::WriteVersionQueryValue(const WCHAR* szValueName, ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_PEInfo.CheckVersionInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteNothing();
        return hr;
    }

    LPBYTE ptr, ptr_end;
    ptr = (LPBYTE)(GetDetails()->GetVersionInfoBlock().GetData());
    ptr_end = (LPBYTE)ptr + GetDetails()->GetVersionInfoBlock().GetCount();

    WCHAR* pValue = NULL;
    FindVersionQueryValueRec(szValueName, wcslen(szValueName), ptr, ptr_end, 0, &pValue);

    if (pValue == NULL)
    {
        return output.WriteNothing();
    }
    else
    {
        return output.WriteString(pValue);
    }
}
