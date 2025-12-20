// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2021 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "PEInfo.h"
#include "FileInfo.h"
#include "FSUtils.h"

#include "SystemDetails.h"

#include "CryptoHashStream.h"
#include "FuzzyHashStream.h"
#include "MemoryStream.h"
#include "CacheStream.h"

#include "FileFormat/PeParser.h"
#include "ByteStreamHelpers.h"

#pragma comment(lib, "Crypt32.lib")

#include "Log/Log.h"

using namespace Orc;

PEInfo::PEInfo(FileInfo& fileInfo)
    : m_FileInfo(fileInfo)
{
}

PEInfo::~PEInfo() {}

bool PEInfo::HasFileVersionInfo()
{
    if (m_FileInfo.IsDirectory())
        return false;
    if (FAILED(CheckVersionInformation()))
        return false;
    if (m_FileInfo.GetDetails()->FileVersionAvailable())
        return true;
    return false;
}

bool PEInfo::HasExeHeader()
{
    if (m_FileInfo.IsDirectory())
        return false;

    if (FAILED(CheckPEInformation()))
        return false;

    auto& imageDosHeader = m_FileInfo.GetDetails()->GetImageDosHeader();
    if (!imageDosHeader)
    {
        return false;
    }

    // XXX is IMAGE_OS2_SIGNATURE in dosHdr->e_magic or ntHdr->signature ? (if it's the latter, only check for
    // DOS_SIG here)
    if ((imageDosHeader->e_magic == IMAGE_DOS_SIGNATURE) || (imageDosHeader->e_magic == IMAGE_OS2_SIGNATURE)
        || (imageDosHeader->e_magic == IMAGE_OS2_SIGNATURE_LE))
    {
        return true;
    }

    return false;
}

bool PEInfo::HasPEHeader()
{
    if (m_FileInfo.IsDirectory())
        return false;
    if (FAILED(CheckPEInformation()))
        return false;

    return m_FileInfo.GetDetails()->GetImageFileHeader().has_value();
}

const IMAGE_DOS_HEADER* Orc::PEInfo::GetImageDosHeader() const
{
    if (m_FileInfo.GetDetails() == nullptr)
        return NULL;
    if (!m_FileInfo.GetDetails()->GetImageDosHeader().has_value())
    {
        return NULL;
    }

    return &m_FileInfo.GetDetails()->GetImageDosHeader().value();
}

const IMAGE_FILE_HEADER* Orc::PEInfo::GetImageFileHeader() const
{
    if (m_FileInfo.GetDetails() == nullptr)
        return NULL;
    if (!m_FileInfo.GetDetails()->GetImageFileHeader().has_value())
    {
        return NULL;
    }

    return &m_FileInfo.GetDetails()->GetImageFileHeader().value();
}

const IMAGE_OPTIONAL_HEADER32* Orc::PEInfo::GetPe32OptionalHeader() const
{
    auto header = m_FileInfo.GetDetails()->GetOptionalImageHeader();
    if (!header)
    {
        return NULL;
    }

    return std::get_if<IMAGE_OPTIONAL_HEADER32>(&header.value());
}

const IMAGE_OPTIONAL_HEADER64* Orc::PEInfo::GetPe64OptionalHeader() const
{
    auto header = m_FileInfo.GetDetails()->GetOptionalImageHeader();
    if (!header)
    {
        return NULL;
    }

    return std::get_if<IMAGE_OPTIONAL_HEADER64>(&header.value());
}

HRESULT PEInfo::CheckPEInformation()
{
    if (m_FileInfo.GetDetails() == nullptr)
        return E_POINTER;
    if (m_FileInfo.GetDetails()->PEChecked())
        return S_OK;
    return OpenPEInformation();
}

HRESULT PEInfo::OpenPEInformation()
{
    HRESULT hr = E_FAIL;

    if (m_FileInfo.IsDirectory())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);

    if (FAILED(hr = m_FileInfo.CheckStream()))
        return hr;

    if (m_FileInfo.GetDetails() == nullptr)
        return E_POINTER;

    m_FileInfo.GetDetails()->SetPEChecked(true);

    CBinaryBuffer buf;
    if (!buf.SetCount(0x400))
        return E_OUTOFMEMORY;

    ULONGLONG ullBytesRead = 0L;

    std::shared_ptr<ByteStream> directStream = m_FileInfo.GetDetails()->GetDataStream();

    CacheStream stream(*directStream, 2048);  // 2048 bytes cache is the sweet spot when measuring I/O

    // > Store first bytes in m_FirstBytes
    {
        if (FAILED(hr = stream.SetFilePointer(0LL, FILE_BEGIN, NULL)))
            return hr;

        if (FAILED(hr = stream.Read(buf.GetData(), buf.GetCount(), &ullBytesRead)))
            return hr;

        buf.SetCount(static_cast<size_t>(ullBytesRead));

        if (!buf.empty())
        {
            CBinaryBuffer fb;
            if (!fb.SetCount(std::min(static_cast<size_t>(BYTES_IN_FIRSTBYTES), buf.GetCount())))
            {
                return E_OUTOFMEMORY;
            }

            CopyMemory(fb.GetData(), buf.GetData(), fb.GetCount());
            m_FileInfo.GetDetails()->SetFirstBytes(std::move(fb));
        }
    }

    std::error_code ec;
    PeParser parser(stream, ec);
    if (ec)
    {
        // BEWARE: Don't log that could be very verbose
        // Log::Debug("Failed to parse PE file [{}]", ec);
        buf.RemoveAll();
        return S_OK;
    }

    m_FileInfo.GetDetails()->SetImageDosHeader(parser.ImageDosHeader());
    if (parser.ImageFileHeader())
    {
        m_FileInfo.GetDetails()->SetImageFileHeader(*parser.ImageFileHeader());
    }

    if (parser.ImageOptionalHeader())
    {
        m_FileInfo.GetDetails()->SetOptionalImageHeader(*parser.ImageOptionalHeader());
    }

    auto securityDirectoryChunk = parser.GetSecurityDirectoryChunk();
    if (securityDirectoryChunk)
    {
        m_FileInfo.GetDetails()->SetSecurityDirectoryChunk(*securityDirectoryChunk);
    }

    // IDEA: could look firstly for lang #0 (neutral) or lang #1033 (en-US) instead of blindly taking the first
    auto versionInfoChunk =
        parser.GetResourceDataChunk(reinterpret_cast<WORD>(RT_VERSION), static_cast<WORD>(VS_VERSION_INFO));
    if (versionInfoChunk)
    {
        m_FileInfo.GetDetails()->SetVersionInfoChunk(*versionInfoChunk);
    }

    return S_OK;
}

HRESULT PEInfo::CheckVersionInformation()
{
    if (m_FileInfo.GetDetails() == nullptr)
        return E_POINTER;
    if (m_FileInfo.GetDetails()->FileVersionChecked())
        return S_OK;
    return OpenVersionInformation();
}

HRESULT PEInfo::OpenVersionInformation()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_FileInfo.CheckStream()))
        return hr;
    if (FAILED(hr = CheckPEInformation()))
        return hr;

    m_FileInfo.GetDetails()->SetFileVersionChecked(true);

    if (!HasPEHeader())
        return S_OK;

    if (!m_FileInfo.GetDetails()->GetVersionInfoChunk().has_value())
    {
        return S_OK;
    }

    std::shared_ptr<ByteStream> directStream = m_FileInfo.GetDetails()->GetDataStream();
    if (!directStream)
    {
        return E_POINTER;
    }

    auto stream = std::make_shared<CacheStream>(*directStream, 4096);  // sweet spot for version info

    std::error_code ec;
    PeVersionInfo versionInfo(*stream, *m_FileInfo.GetDetails()->GetVersionInfoChunk(), ec);
    if (ec)
    {
        return ToHRESULT(ec);
    }

    m_FileInfo.GetDetails()->SetVersionInfo(std::move(versionInfo));
    return S_OK;
}

HRESULT PEInfo::CheckSecurityDirectory()
{
    if (m_FileInfo.GetDetails() == nullptr)
        return E_POINTER;
    if (m_FileInfo.GetDetails()->SecurityDirectoryChecked())
        return S_OK;
    return OpenSecurityDirectory();
}

HRESULT PEInfo::OpenSecurityDirectory()
{
    std::error_code ec;
    HRESULT hr = E_FAIL;

    if (m_FileInfo.IsDirectory())
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);

    if (FAILED(hr = m_FileInfo.CheckStream()))
        return hr;
    if (FAILED(hr = CheckPEInformation()))
        return hr;

    m_FileInfo.GetDetails()->SetSecurityDirectoryChecked(true);

    if (!HasPEHeader())
        return S_OK;

    if (!m_FileInfo.GetDetails()->GetSecurityDirectoryChunk().has_value())
    {
        return S_OK;
    }

    std::shared_ptr<ByteStream> stream = m_FileInfo.GetDetails()->GetDataStream();

    const auto& chunk = *m_FileInfo.GetDetails()->GetSecurityDirectoryChunk();
    std::vector<uint8_t> buffer;
    ReadChunkAt(*stream, chunk.offset, chunk.length, buffer, ec);
    if (ec)
    {
        return ToHRESULT(ec);
    }

    CBinaryBuffer bb;
    bb.SetData(buffer.data(), buffer.size());
    m_FileInfo.GetDetails()->SetSecurityDirectory(std::move(bb));

    return S_OK;
}

HRESULT PEInfo::OpenAllHash(Intentions localIntentions)
{
    HRESULT hr = E_FAIL;

    DWORD dwMajor = 0L, dwMinor = 0L;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    const auto& details = m_FileInfo.GetDetails();

    if (details->PeHashAvailable() && details->HashAvailable())
        return S_OK;

    CryptoHashStream::Algorithm algs = CryptoHashStream::Algorithm::Undefined;
    if (HasFlag(localIntentions, Intentions::FILEINFO_MD5))
        algs |= CryptoHashStream::Algorithm::MD5;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SHA1))
        algs |= CryptoHashStream::Algorithm::SHA1;
    if (HasFlag(localIntentions, Intentions::FILEINFO_SHA256))
        algs |= CryptoHashStream::Algorithm::SHA256;

    CryptoHashStream::Algorithm pe_algs = CryptoHashStream::Algorithm::Undefined;
    if (HasFlag(localIntentions, Intentions::FILEINFO_PE_MD5))
        pe_algs |= CryptoHashStream::Algorithm::MD5;
    if (HasFlag(localIntentions, Intentions::FILEINFO_PE_SHA1))
        pe_algs |= CryptoHashStream::Algorithm::SHA1;
    if (HasFlag(localIntentions, Intentions::FILEINFO_PE_SHA256))
        pe_algs |= CryptoHashStream::Algorithm::SHA256;

    FuzzyHashStream::Algorithm fuzzy_algs = FuzzyHashStream::Algorithm::Undefined;

#ifdef ORC_BUILD_SSDEEP
    if (HasFlag(localIntentions, Intentions::FILEINFO_SSDEEP))
        fuzzy_algs = fuzzy_algs | FuzzyHashStream::Algorithm::SSDeep;
#endif

    if (HasAnyFlag(
            localIntentions, Intentions::FILEINFO_AUTHENTICODE_STATUS | Intentions::FILEINFO_AUTHENTICODE_SIGNER))
    {
        pe_algs |=
            CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1 | CryptoHashStream::Algorithm::SHA256;
    }

    auto stream = m_FileInfo.GetDetails()->GetDataStream();
    if (stream == nullptr)
        return E_POINTER;

    stream->SetFilePointer(0L, FILE_BEGIN, NULL);

    // Load data in a memory stream

    auto memstream = std::make_shared<MemoryStream>();
    if (memstream == nullptr)
        return E_OUTOFMEMORY;

    if (FAILED(hr = memstream->OpenForReadWrite(stream->GetSize())))
        return hr;

    ULONGLONG ullWritten = 0LL;
    if (FAILED(hr = stream->CopyTo(*memstream, &ullWritten)))
        return hr;

    // Crypto hash
    auto pData = memstream->GetBuffer();
    {
        auto hashstream = std::make_shared<CryptoHashStream>();
        hashstream->OpenToWrite(algs, nullptr);

        ULONGLONG ullHashed = 0LL;
        hashstream->Write(pData.GetData(), ullWritten, &ullHashed);

        if (HasFlag(algs, CryptoHashStream::Algorithm::MD5))
        {
            hr = hashstream->GetHash(CryptoHashStream::Algorithm::MD5, m_FileInfo.GetDetails()->MD5());
            if (FAILED(hr) && hr != MK_E_UNAVAILABLE)
            {
                return hr;
            }
        }

        if (HasFlag(algs, CryptoHashStream::Algorithm::SHA1))
        {
            hr = hashstream->GetHash(CryptoHashStream::Algorithm::SHA1, m_FileInfo.GetDetails()->SHA1());
            if (FAILED(hr) && hr != MK_E_UNAVAILABLE)
            {
                return hr;
            }
        }

        if (HasFlag(algs, CryptoHashStream::Algorithm::SHA256))
        {
            hashstream->GetHash(CryptoHashStream::Algorithm::SHA256, m_FileInfo.GetDetails()->SHA256());
            if (FAILED(hr) && hr != MK_E_UNAVAILABLE)
            {
                return hr;
            }
        }
    }

    // Fuzzy hash
    {
        auto hashstream = std::make_shared<FuzzyHashStream>();
        hashstream->OpenToWrite(fuzzy_algs, nullptr);

        ULONGLONG ullHashed = 0LL;
        hashstream->Write(pData.GetData(), ullWritten, &ullHashed);

        if (HasFlag(fuzzy_algs, FuzzyHashStream::Algorithm::SSDeep))
        {
            hr = hashstream->GetHash(FuzzyHashStream::Algorithm::SSDeep, m_FileInfo.GetDetails()->SSDeep());
            if (FAILED(hr) && hr != MK_E_UNAVAILABLE)
            {
                return hr;
            }
        }
    }

    // Pe Hash
    {
        std::error_code ec;
        PeParser pe(*memstream, ec);
        if (ec)
        {
            Log::Debug(L"Failed to parse pe hash '{}' [{}]", m_FileInfo.m_szFullName, ec);
            return ec.value();
        }

        PeParser::PeHash hashes;
        pe.GetAuthenticodeHash(pe_algs, hashes, ec);
        if (ec)
        {
            Log::Error(L"Failed to compute pe hash '{}' [{}]", m_FileInfo.m_szFullName, ec);
            return ec.value();
        }

        if (HasFlag(pe_algs, CryptoHashStream::Algorithm::MD5))
        {
            auto& bb = m_FileInfo.GetDetails()->PeMD5();
            bb.SetCount(hashes.md5->size());
            std::copy(std::cbegin(*hashes.md5), std::cend(*hashes.md5), std::begin(bb));
        }

        if (HasFlag(pe_algs, CryptoHashStream::Algorithm::SHA1) && hashes.sha1.has_value())
        {
            auto& bb = m_FileInfo.GetDetails()->PeSHA1();
            bb.SetCount(hashes.sha1->size());
            std::copy(std::cbegin(*hashes.sha1), std::cend(*hashes.sha1), std::begin(bb));
        }

        if (HasFlag(pe_algs, CryptoHashStream::Algorithm::SHA256) && hashes.sha256.has_value())
        {
            auto& bb = m_FileInfo.GetDetails()->PeSHA256();
            bb.SetCount(hashes.sha256->size());
            std::copy(std::cbegin(*hashes.sha256), std::cend(*hashes.sha256), std::begin(bb));
        }
    }

    return S_OK;
}

HRESULT PEInfo::OpenPeHash(Intentions localIntentions)
{
    HRESULT hr = E_FAIL;
    if (m_FileInfo.GetDetails()->PeHashAvailable())
        return S_OK;

    if (FAILED(hr = CheckPEInformation()))
        return hr;
    if (!HasPEHeader())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
    }

    DWORD dwMajor = 0L, dwMinor = 0L;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    CryptoHashStream::Algorithm algs = CryptoHashStream::Algorithm::Undefined;
    if (HasFlag(localIntentions, Intentions::FILEINFO_PE_MD5))
        algs |= CryptoHashStream::Algorithm::MD5;
    if (HasFlag(localIntentions, Intentions::FILEINFO_PE_SHA1))
        algs |= CryptoHashStream::Algorithm::SHA1;
    if (HasFlag(localIntentions, Intentions::FILEINFO_PE_SHA256))
        algs |= CryptoHashStream::Algorithm::SHA256;
    if (HasAnyFlag(
            localIntentions, Intentions::FILEINFO_AUTHENTICODE_STATUS | Intentions::FILEINFO_AUTHENTICODE_SIGNER))
    {
        algs |=
            CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1 | CryptoHashStream::Algorithm::SHA256;
    }

    auto stream = m_FileInfo.GetDetails()->GetDataStream();
    if (stream == nullptr)
        return E_POINTER;

    std::error_code ec;
    CacheStream cache(*stream, 1048576);
    PeParser pe(cache, ec);
    if (ec)
    {
        Log::Error(L"Failed to parse PE '{}' [{}]", m_FileInfo.m_szFullName, ec);
        return ec.value();
    }

    PeParser::PeHash hashes;
    pe.GetAuthenticodeHash(algs, hashes, ec);
    if (ec)
    {
        Log::Error(L"Failed to compute PE hashes '{}' [{}]", m_FileInfo.m_szFullName, ec);
        return ec.value();
    }

    if (HasFlag(algs, CryptoHashStream::Algorithm::MD5))
    {
        auto& bb = m_FileInfo.GetDetails()->PeMD5();
        bb.SetCount(hashes.md5->size());
        std::copy(std::cbegin(*hashes.md5), std::cend(*hashes.md5), std::begin(bb));
    }

    if (HasFlag(algs, CryptoHashStream::Algorithm::SHA1) && hashes.sha1.has_value())
    {
        auto& bb = m_FileInfo.GetDetails()->PeSHA1();
        bb.SetCount(hashes.sha1->size());
        std::copy(std::cbegin(*hashes.sha1), std::cend(*hashes.sha1), std::begin(bb));
    }

    if (HasFlag(algs, CryptoHashStream::Algorithm::SHA256) && hashes.sha256.has_value())
    {
        auto& bb = m_FileInfo.GetDetails()->PeSHA256();
        bb.SetCount(hashes.sha256->size());
        std::copy(std::cbegin(*hashes.sha256), std::cend(*hashes.sha256), std::begin(bb));
    }

    return S_OK;
}
