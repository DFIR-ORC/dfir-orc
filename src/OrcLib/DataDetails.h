//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "BinaryBuffer.h"
#include "Authenticode.h"
#include "CryptoUtilities.h"

#include <memory>

#pragma managed(push, off)

namespace Orc {

class ByteStream;

class DataDetails
{

private:
    std::shared_ptr<ByteStream> m_DataStream;
    std::shared_ptr<ByteStream> m_RawStream;

    CBinaryBuffer m_FirstBytes;

    CBinaryBuffer m_DosHeader; /* 0x40 first bytes */
    CBinaryBuffer m_PEHeader; /* PE header + section table */
    bool m_bPEChecked = false;

    CBinaryBuffer m_Sha1;
    CBinaryBuffer m_Sha256;
    CBinaryBuffer m_MD5;

    std::wstring m_ssdeep;
    std::wstring m_tlsh;

    Authenticode::PE_Hashs m_PEHashs;
    Authenticode::AuthenticodeData m_AuthData;
    bool m_bHasAuthData = false;

    bool m_bHashChecked = false;

    CBinaryBuffer m_pVersionInfoBlock;
    VS_FIXEDFILEINFO* m_pFFI;
    bool m_bFileVersionChecked = false;

    CBinaryBuffer m_pSecurityDirectory;
    bool m_bSecurityDirectoryChecked = false;

public:
    HRESULT SetDataStream(const std::shared_ptr<ByteStream>& dataStream)
    {
        m_DataStream = dataStream;
        return S_OK;
    }
    const std::shared_ptr<ByteStream>& GetDataStream() const { return m_DataStream; }

    HRESULT SetRawStream(const std::shared_ptr<ByteStream>& rawStream)
    {
        m_RawStream = rawStream;
        return S_OK;
    }
    const std::shared_ptr<ByteStream>& GetRawStream() const { return m_RawStream; }

    HRESULT SetDosHeader(CBinaryBuffer&& buffer)
    {
        std::swap(m_DosHeader, buffer);
        return S_OK;
    }
    CBinaryBuffer& GetDosHeader() { return m_DosHeader; }

    HRESULT SetPEHeader(CBinaryBuffer&& buffer)
    {
        std::swap(m_PEHeader, buffer);
        return S_OK;
    }
    CBinaryBuffer& GetPEHeader() { return m_PEHeader; }

    void SetPEChecked(bool bChecked) { m_bPEChecked = bChecked; }
    bool PEChecked() const { return m_bPEChecked; }

    HRESULT SetFirstBytes(CBinaryBuffer&& buffer)
    {
        std::swap(m_FirstBytes, buffer);
        return S_OK;
    }
    bool FirstBytesAvailable() const { return m_FirstBytes.GetCount() == BYTES_IN_FIRSTBYTES; }
    CBinaryBuffer& FirstBytes() { return m_FirstBytes; }

    HRESULT SetMD5(CBinaryBuffer&& buffer)
    {
        std::swap(m_MD5, buffer);
        return S_OK;
    }
    CBinaryBuffer& MD5() { return m_MD5; }
    HRESULT SetSHA1(CBinaryBuffer&& buffer)
    {
        std::swap(m_Sha1, buffer);
        return S_OK;
    }
    CBinaryBuffer& SHA1() { return m_Sha1; }
    HRESULT SetSHA256(CBinaryBuffer&& buffer)
    {
        std::swap(m_Sha256, buffer);
        return S_OK;
    }
    CBinaryBuffer& SHA256() { return m_Sha256; }

    HRESULT SetSSDeep(std::wstring&& ssdeep)
    {
        std::swap(m_ssdeep, ssdeep);
        return S_OK;
    }
    std::wstring& SSDeep() { return m_ssdeep; }

    HRESULT SetTLSH(std::wstring&& tlsh)
    {
        std::swap(m_tlsh, tlsh);
        return S_OK;
    }
    std::wstring& TLSH() { return m_tlsh; }

    bool HashAvailable() const
    {
        return m_MD5.GetCount() == BYTES_IN_MD5_HASH || m_Sha1.GetCount() == BYTES_IN_SHA1_HASH
            || m_Sha256.GetCount() == BYTES_IN_SHA256_HASH;
    }

    void SetHashChecked(bool bChecked) { m_bHashChecked = bChecked; }
    bool HashChecked() const { return m_bHashChecked; }

    HRESULT SetPeSHA1(CBinaryBuffer&& buffer)
    {
        std::swap(m_PEHashs.sha1, buffer);
        return S_OK;
    }
    CBinaryBuffer& PeSHA1() { return m_PEHashs.sha1; }
    HRESULT SetPeSHA256(CBinaryBuffer&& buffer)
    {
        std::swap(m_PEHashs.sha256, buffer);
        return S_OK;
    }
    CBinaryBuffer& PeSHA256() { return m_PEHashs.sha256; }
    HRESULT SetPeMD5(CBinaryBuffer&& buffer)
    {
        std::swap(m_PEHashs.md5, buffer);
        return S_OK;
    }
    CBinaryBuffer& PeMD5() { return m_PEHashs.md5; }

    const Authenticode::PE_Hashs& GetPEHashs() const { return m_PEHashs; };

    HRESULT SetPEHashs(const Authenticode::PE_Hashs& hashs)
    {
        m_PEHashs = hashs;
        return S_OK;
    }

    HRESULT SetPEHashs(Authenticode::PE_Hashs&& hashs)
    {
        std::swap(m_PEHashs, hashs);
        return S_OK;
    }

    bool PeHashAvailable() const
    {
        return m_PEHashs.sha1.GetCount() == BYTES_IN_SHA1_HASH || m_PEHashs.sha256.GetCount() == BYTES_IN_SHA256_HASH
            || m_PEHashs.md5.GetCount() == BYTES_IN_MD5_HASH;
    }

    HRESULT SetAuthenticodeData(const Authenticode::AuthenticodeData& data)
    {
        m_AuthData = data;
        m_bHasAuthData = true;
        return S_OK;
    };

    HRESULT SetAuthenticodeData(Authenticode::AuthenticodeData&& data)
    {
        std::swap(m_AuthData, data);
        m_bHasAuthData = true;
        return S_OK;
    };

    const Authenticode::AuthenticodeData& GetAuthenticodeData() const { return m_AuthData; };
    bool HasAuthenticodeData() const { return m_bHasAuthData; };

    HRESULT SetVersionInfoBlock(CBinaryBuffer&& buffer)
    {
        std::swap(m_pVersionInfoBlock, buffer);
        return S_OK;
    }
    CBinaryBuffer& GetVersionInfoBlock() { return m_pVersionInfoBlock; }
    HRESULT SetFixedFileInfo(VS_FIXEDFILEINFO* newval)
    {
        m_pFFI = newval;
        return S_OK;
    }
    VS_FIXEDFILEINFO* GetFixedFileInfo() { return m_pFFI; }

    void SetFileVersionChecked(bool bChecked) { m_bFileVersionChecked = bChecked; }
    bool FileVersionChecked() { return m_bFileVersionChecked; }
    bool FileVersionAvailable() { return m_bFileVersionChecked && (m_pFFI != NULL); }

    void SetSecurityDirectoryChecked(bool bChecked) { m_bSecurityDirectoryChecked = bChecked; }
    bool SecurityDirectoryChecked() { return m_bSecurityDirectoryChecked; }
    bool SecurityDirectoryAvailable() { return m_bSecurityDirectoryChecked && (m_pSecurityDirectory.GetCount() != 0L); }
    HRESULT SetSecurityDirectory(CBinaryBuffer&& buffer)
    {
        std::swap(m_pSecurityDirectory, buffer);
        return S_OK;
    }
    const CBinaryBuffer& SecurityDirectory() const { return m_pSecurityDirectory; }

public:
    DataDetails() {};
    ~DataDetails() {};
};

}  // namespace Orc

#pragma managed(pop)
