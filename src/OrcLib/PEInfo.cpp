//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2021 ANSSI. All Rights Reserved.
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
    CBinaryBuffer& dosBuf = m_FileInfo.GetDetails()->GetDosHeader();
    if (dosBuf.GetCount() >= 2)
    {
        PIMAGE_DOS_HEADER dosHdr = (PIMAGE_DOS_HEADER)dosBuf.GetData();
        // XXX is IMAGE_OS2_SIGNATURE in dosHdr->e_magic or ntHdr->signature ? (if it's the latter, only check for
        // DOS_SIG here)
        if ((dosHdr->e_magic == IMAGE_DOS_SIGNATURE) || (dosHdr->e_magic == IMAGE_OS2_SIGNATURE)
            || (dosHdr->e_magic == IMAGE_OS2_SIGNATURE_LE))
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
    CBinaryBuffer& peBuf = m_FileInfo.GetDetails()->GetPEHeader();
    if ((peBuf.GetCount() >= 4) && !memcmp(peBuf.GetData(), "PE\0\0", 4))
        return true;
    return false;
}

PIMAGE_NT_HEADERS32 PEInfo::GetPe32Header() const
{
    if (m_FileInfo.GetDetails() == nullptr)
        return NULL;
    if (m_FileInfo.GetDetails()->GetPEHeader().GetCount() < sizeof(IMAGE_NT_HEADERS32))
        return NULL;
    return (PIMAGE_NT_HEADERS32)m_FileInfo.GetDetails()->GetPEHeader().GetData();
}

PIMAGE_NT_HEADERS64 PEInfo::GetPe64Header() const
{
    if (m_FileInfo.GetDetails() == nullptr)
        return NULL;
    if (m_FileInfo.GetDetails()->GetPEHeader().GetCount() < sizeof(IMAGE_NT_HEADERS64))
        return NULL;
    return (PIMAGE_NT_HEADERS64)m_FileInfo.GetDetails()->GetPEHeader().GetData();
}

PIMAGE_SECTION_HEADER PEInfo::GetPeSections() const
{
    if (m_FileInfo.GetDetails() == nullptr)
        return NULL;
    if (m_FileInfo.GetDetails()->GetPEHeader().GetCount() < sizeof(IMAGE_NT_HEADERS32))
        return NULL;
    PIMAGE_NT_HEADERS32 pehdr = (PIMAGE_NT_HEADERS32)(m_FileInfo.GetDetails()->GetPEHeader().GetData());
    DWORD dwSectionOffset = 4 + sizeof(IMAGE_FILE_HEADER) + pehdr->FileHeader.SizeOfOptionalHeader;
    if (m_FileInfo.GetDetails()->GetPEHeader().GetCount()
        < (dwSectionOffset + pehdr->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)))
        return NULL;
    return (PIMAGE_SECTION_HEADER)((LPBYTE)pehdr + dwSectionOffset);
}

size_t PEInfo::PeRvaToFileOffset(size_t RvaStart, size_t Length, PIMAGE_SECTION_HEADER Sections, size_t SectionCount)
{
    size_t i;
    for (i = 0; i < SectionCount; i++)
    {
        if ((RvaStart >= Sections[i].VirtualAddress)
            && (RvaStart + Length <= static_cast<size_t>(Sections[i].VirtualAddress + Sections[i].Misc.VirtualSize)))
        {
            return Sections[i].PointerToRawData + RvaStart - Sections[i].VirtualAddress;
        }
    }

    return (size_t)-1;
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

    std::shared_ptr<ByteStream> stream = m_FileInfo.GetDetails()->GetDataStream();
    if (FAILED(hr = stream->SetFilePointer(0LL, FILE_BEGIN, NULL)))
        return hr;
    if (FAILED(hr = stream->Read(buf.GetData(), buf.GetCount(), &ullBytesRead)))
        return hr;
    buf.SetCount(static_cast<size_t>(ullBytesRead));

    if (buf.GetCount() >= BYTES_IN_FIRSTBYTES)
    {
        CBinaryBuffer fb;
        if (!fb.SetCount(BYTES_IN_FIRSTBYTES))
            return E_OUTOFMEMORY;
        CopyMemory(fb.GetData(), buf.GetData(), BYTES_IN_FIRSTBYTES);
        m_FileInfo.GetDetails()->SetFirstBytes(std::move(fb));
    }

    if (buf.GetCount() < sizeof(IMAGE_DOS_HEADER))
    {
        buf.RemoveAll();
        return S_OK;
    }

    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)buf.GetData();

    if ((IMAGE_DOS_SIGNATURE == pDos->e_magic) || (IMAGE_OS2_SIGNATURE == pDos->e_magic)
        || (IMAGE_OS2_SIGNATURE_LE == pDos->e_magic))
    {
        CBinaryBuffer dosbuf;
        if (!dosbuf.SetCount(sizeof(IMAGE_DOS_HEADER)))
            return E_OUTOFMEMORY;
        CopyMemory(dosbuf.GetData(), buf.GetData(), sizeof(IMAGE_DOS_HEADER));
        m_FileInfo.GetDetails()->SetDosHeader(std::move(dosbuf));
    }

    if (pDos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) >= stream->GetSize())
    {
        return S_OK;
    }

    if (IMAGE_DOS_SIGNATURE == pDos->e_magic)
    {
        /* retrieve PE headers + section table */
        CBinaryBuffer pebuf;
        if (buf.GetCount() >= pDos->e_lfanew + sizeof(IMAGE_NT_HEADERS64))
        {
            /* pe header is already in buf */
            if (!pebuf.SetCount(buf.GetCount() - pDos->e_lfanew))
                return E_OUTOFMEMORY;
            CopyMemory(pebuf.GetData(), buf.GetData() + pDos->e_lfanew, buf.GetCount() - pDos->e_lfanew);
        }
        else
        {
            /* read pe header from stream */
            if (!pebuf.SetCount(0x400))
                return E_OUTOFMEMORY;
            ullBytesRead = 0;
            if (FAILED(hr = stream->SetFilePointer(pDos->e_lfanew, FILE_BEGIN, NULL)))
                return hr;
            if (FAILED(hr = stream->Read(pebuf.GetData(), pebuf.GetCount(), &ullBytesRead)))
                return hr;
            pebuf.SetCount(static_cast<size_t>(ullBytesRead));
        }

        if (pebuf.GetCount() >= sizeof(IMAGE_NT_HEADERS64))
        {
            PIMAGE_NT_HEADERS64 pehdr = (PIMAGE_NT_HEADERS64)pebuf.GetData();
            if (pehdr->Signature == IMAGE_NT_SIGNATURE)
            {
                size_t dwSectionEndOffset = 4 + sizeof(IMAGE_FILE_HEADER) + pehdr->FileHeader.SizeOfOptionalHeader
                    + pehdr->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
                if (dwSectionEndOffset <= pebuf.GetCount())
                {
                    pebuf.SetCount(dwSectionEndOffset);
                }
                else
                {
                    /* try to retrieve full section table */
                    if ((pehdr->FileHeader.NumberOfSections < 0x100) && pebuf.SetCount(dwSectionEndOffset))
                    {
                        ullBytesRead = 0;
                        if (FAILED(hr = stream->SetFilePointer(pDos->e_lfanew, FILE_BEGIN, NULL)))
                            return hr;
                        if (FAILED(hr = stream->Read(pebuf.GetData(), dwSectionEndOffset, &ullBytesRead)))
                            return hr;
                    }
                }
                m_FileInfo.GetDetails()->SetPEHeader(std::move(pebuf));
            }
        }
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

    PIMAGE_NT_HEADERS32 pe32 = GetPe32Header();
    PIMAGE_NT_HEADERS64 pe64 = GetPe64Header();
    PIMAGE_SECTION_HEADER sections = GetPeSections();
    if (sections == NULL)
        return S_OK;

    size_t rsrc_pe_offset = 0;
    PIMAGE_DATA_DIRECTORY rsrc_datadir = NULL;

    if (pe32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        if (pe64->OptionalHeader.NumberOfRvaAndSizes < IMAGE_DIRECTORY_ENTRY_RESOURCE)
            return S_OK;
        rsrc_datadir = &pe64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    }
    else
    {
        if (pe32->OptionalHeader.NumberOfRvaAndSizes < IMAGE_DIRECTORY_ENTRY_RESOURCE)
            return S_OK;
        rsrc_datadir = &pe32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    }

    rsrc_pe_offset = PeRvaToFileOffset(
        rsrc_datadir->VirtualAddress, rsrc_datadir->Size, sections, pe32->FileHeader.NumberOfSections);
    if (rsrc_pe_offset == -1)
        return S_OK;

    CBinaryBuffer rsrcBuf;
    if (!rsrcBuf.SetCount(1024))
        return E_OUTOFMEMORY;
    std::shared_ptr<ByteStream> stream = m_FileInfo.GetDetails()->GetDataStream();
    ULONGLONG ullBytesRead;
    size_t rsrc_rsrc_offset = 0;

    // search for the VERSION ressource
    ullBytesRead = 0;
    if (FAILED(hr = stream->SetFilePointer(rsrc_pe_offset + rsrc_rsrc_offset, FILE_BEGIN, NULL)))
        return hr;
    if (FAILED(hr = stream->Read(rsrcBuf.GetData(), rsrcBuf.GetCount(), &ullBytesRead)))
        return hr;

    if (ullBytesRead < sizeof(IMAGE_RESOURCE_DIRECTORY))
        return S_OK;
    PIMAGE_RESOURCE_DIRECTORY rsrc_dir = (PIMAGE_RESOURCE_DIRECTORY)rsrcBuf.GetData();
    size_t rsrc_entry_count = static_cast<size_t>(rsrc_dir->NumberOfIdEntries) + rsrc_dir->NumberOfNamedEntries;
    rsrc_entry_count = std::min(
        rsrc_entry_count,
        ((static_cast<size_t>(ullBytesRead) - sizeof(IMAGE_RESOURCE_DIRECTORY))
         / sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY)));

    const auto RESOURCE_ID_VERSION = 16;
    rsrc_rsrc_offset = (size_t)-1;
    for (size_t i = 0; i < rsrc_entry_count; i++)
    {
        PIMAGE_RESOURCE_DIRECTORY_ENTRY pde = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(
            (LPBYTE)rsrc_dir + sizeof(IMAGE_RESOURCE_DIRECTORY) + i * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY));
        if ((!pde->NameIsString) && (pde->Id == RESOURCE_ID_VERSION) && (pde->DataIsDirectory))
        {
            rsrc_rsrc_offset = pde->OffsetToDirectory;
            break;
        }
    }

    if (rsrc_rsrc_offset == -1)
        return S_OK;

    // VERSION[0]
    ullBytesRead = 0;
    if (FAILED(hr = stream->SetFilePointer(rsrc_pe_offset + rsrc_rsrc_offset, FILE_BEGIN, NULL)))
        return hr;
    if (FAILED(hr = stream->Read(rsrcBuf.GetData(), rsrcBuf.GetCount(), &ullBytesRead)))
        return hr;

    if (ullBytesRead < sizeof(IMAGE_RESOURCE_DIRECTORY))
        return S_OK;
    rsrc_dir = (PIMAGE_RESOURCE_DIRECTORY)rsrcBuf.GetData();
    rsrc_entry_count = static_cast<size_t>(rsrc_dir->NumberOfIdEntries) + rsrc_dir->NumberOfNamedEntries;
    rsrc_entry_count = std::min(
        rsrc_entry_count,
        ((static_cast<size_t>(ullBytesRead) - sizeof(IMAGE_RESOURCE_DIRECTORY))
         / sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY)));

    rsrc_rsrc_offset = (size_t)-1;
    for (size_t i = 0; i < rsrc_entry_count; i++)
    {
        PIMAGE_RESOURCE_DIRECTORY_ENTRY pde = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(
            (LPBYTE)rsrc_dir + sizeof(IMAGE_RESOURCE_DIRECTORY) + i * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY));
        if (pde->DataIsDirectory)
        {
            rsrc_rsrc_offset = pde->OffsetToDirectory;  // take any available children
            break;
        }
    }
    if (rsrc_rsrc_offset == -1)
        return S_OK;

    // VERSION[0][LANG]
    ullBytesRead = 0;
    if (FAILED(hr = stream->SetFilePointer(rsrc_pe_offset + rsrc_rsrc_offset, FILE_BEGIN, NULL)))
        return hr;
    if (FAILED(hr = stream->Read(rsrcBuf.GetData(), rsrcBuf.GetCount(), &ullBytesRead)))
        return hr;

    if (ullBytesRead < sizeof(IMAGE_RESOURCE_DIRECTORY))
        return S_OK;
    rsrc_dir = (PIMAGE_RESOURCE_DIRECTORY)rsrcBuf.GetData();
    rsrc_entry_count = rsrc_dir->NumberOfIdEntries + rsrc_dir->NumberOfNamedEntries;
    rsrc_entry_count = std::min(
        rsrc_entry_count,
        ((static_cast<size_t>(ullBytesRead) - sizeof(IMAGE_RESOURCE_DIRECTORY))
         / sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY)));

    rsrc_rsrc_offset = static_cast<size_t>(-1);
    for (size_t i = 0; i < rsrc_entry_count; i++)
    {
        PIMAGE_RESOURCE_DIRECTORY_ENTRY pde = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(
            (LPBYTE)rsrc_dir + sizeof(IMAGE_RESOURCE_DIRECTORY) + i * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY));
        if ((!pde->NameIsString) && (!pde->DataIsDirectory))
        {
            // TODO check LANG (pde->Id)
            // for now, take any available children
            rsrc_rsrc_offset = pde->OffsetToData;
            break;
        }
    }
    if (rsrc_rsrc_offset == -1)
        return S_OK;

    ullBytesRead = 0;
    if (FAILED(hr = stream->SetFilePointer(rsrc_pe_offset + rsrc_rsrc_offset, FILE_BEGIN, NULL)))
        return hr;
    if (FAILED(hr = stream->Read(rsrcBuf.GetData(), rsrcBuf.GetCount(), &ullBytesRead)))
        return hr;

    if (ullBytesRead < sizeof(IMAGE_RESOURCE_DATA_ENTRY))
        return S_OK;
    PIMAGE_RESOURCE_DATA_ENTRY version_entry = (PIMAGE_RESOURCE_DATA_ENTRY)rsrcBuf.GetData();

    size_t version_off = PeRvaToFileOffset(
        version_entry->OffsetToData, version_entry->Size, sections, pe32->FileHeader.NumberOfSections);
    if (version_off == -1)
        return S_OK;

    CBinaryBuffer versionBuf;
    if (!versionBuf.SetCount(version_entry->Size))
        return S_OK;

    ullBytesRead = 0;
    if (FAILED(hr = stream->SetFilePointer(version_off, FILE_BEGIN, NULL)))
        return hr;
    if (FAILED(hr = stream->Read(versionBuf.GetData(), versionBuf.GetCount(), &ullBytesRead)))
        return hr;

    // 40 = 6 + sizeof(L"VS_VERSION_INFO")
    if ((ullBytesRead != versionBuf.GetCount()) || (ullBytesRead < (sizeof(VS_FIXEDFILEINFO) + 40)))
        return S_OK;
    if (memcmp(versionBuf.GetData() + 6, L"VS_VERSION_INFO", 32))
        return S_OK;

    m_FileInfo.GetDetails()->SetVersionInfoBlock(std::move(versionBuf));

    // XXX pointer into CBinaryBuf.buffer...
    VS_FIXEDFILEINFO* lpFFI =
        (VS_FIXEDFILEINFO*)((LPBYTE)m_FileInfo.GetDetails()->GetVersionInfoBlock().GetData() + 40);
    m_FileInfo.GetDetails()->SetFixedFileInfo(lpFFI);

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

    PIMAGE_NT_HEADERS32 pe32 = GetPe32Header();
    PIMAGE_NT_HEADERS64 pe64 = GetPe64Header();
    PIMAGE_SECTION_HEADER sections = GetPeSections();
    if (sections == NULL)
        return S_OK;

    size_t secdir_pe_offset = 0;

    CBinaryBuffer Buffer;

    if (pe32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        if (pe64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size == 0)
            return S_OK;
        secdir_pe_offset = pe64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress;
        if (!Buffer.SetCount(pe64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size))
            return E_OUTOFMEMORY;
    }
    else
    {
        if (pe32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size == 0)
            return S_OK;
        secdir_pe_offset = pe32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress;
        if (!Buffer.SetCount(pe32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size))
            return E_OUTOFMEMORY;
    }

    std::shared_ptr<ByteStream> stream = m_FileInfo.GetDetails()->GetDataStream();

    if (secdir_pe_offset > stream->GetSize())
    {
        Log::Warn(L"'{}' contains an invalid security directory", m_FileInfo.GetFullName());
        return S_OK;
    }

    // search for the Security Directory
    if (FAILED(hr = stream->SetFilePointer(secdir_pe_offset, FILE_BEGIN, NULL)))
        return hr;

    ULONGLONG ullRead = 0LL;
    while (ullRead < Buffer.GetCount())
    {
        ULONGLONG ullThisRead = 0LL;
        if (FAILED(hr = stream->Read(Buffer.GetData() + ullRead, Buffer.GetCount() - ullRead, &ullThisRead)))
            return hr;

        if (ullThisRead == 0LL)
            break;
        ullRead += ullThisRead;
    }

    m_FileInfo.GetDetails()->SetSecurityDirectory(std::move(Buffer));

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

    if (FAILED(hr = memstream->OpenForReadWrite()))
        return hr;

    if (FAILED(hr = memstream->SetSize(stream->GetSize())))
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
        PeParser pe(memstream, ec);
        if (ec)
        {
            Log::Error(L"Failed to parse pe hash {} [{}]", m_FileInfo.m_szFullName, ec);
            return ec.value();
        }

        PeParser::PeHash hashes;
        pe.GetAuthenticodeHash(pe_algs, hashes, ec);
        if (ec)
        {
            Log::Error(L"Failed to compute pe hash [{}]", m_FileInfo.m_szFullName, ec);
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
    PeParser pe(std::make_shared<CacheStream>(std::move(stream)), ec);
    if (ec)
    {
        Log::Error(L"Failed to parse PE (path: {}) [{}]", m_FileInfo.m_szFullName, ec);
        return ec.value();
    }

    PeParser::PeHash hashes;
    pe.GetAuthenticodeHash(algs, hashes, ec);
    if (ec)
    {
        Log::Error(L"Failed to compute PE hashes (path: {}) [{}]", m_FileInfo.m_szFullName, ec);
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
