//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "PasswordEncryptedStream.h"

#include "LogFileWriter.h"
#include "CryptoUtilities.h"

#include <boost/scope_exit.hpp>

using namespace Orc;

constexpr auto ENCRYPT_ALGORITHM = CALG_AES_256;

HRESULT PasswordEncryptedStream::GetKeyMaterial(const std::wstring& pwd)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CryptoUtilities::AcquireContext(_L_, m_hCryptProv)))
    {
        log::Error(_L_, hr, L"Failed to CryptAcquireContext\r\n");
        return hr;
    }

    HCRYPTHASH hHash = NULL;
    if (!CryptCreateHash(m_hCryptProv, CALG_SHA1, 0, 0, &hHash))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to CryptCreateHash\r\n");
        return hr;
    }
    BOOST_SCOPE_EXIT(hHash) { CryptDestroyHash(hHash); }
    BOOST_SCOPE_EXIT_END;

    if (!CryptHashData(hHash, (BYTE*)pwd.c_str(), (DWORD)pwd.size() * sizeof(WCHAR), 0))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to CryptHashData\r\n");
        return hr;
    }

    if (!CryptDeriveKey(m_hCryptProv, ENCRYPT_ALGORITHM, hHash, 0L, &m_hKey))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to CryptDeriveKey\r\n");
        return hr;
    }

    DWORD dwDataLen = sizeof(DWORD);
    if (!CryptGetKeyParam(m_hKey, KP_BLOCKLEN, (BYTE*)&m_dwBlockLen, &dwDataLen, 0L))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to CryptGetKeyParam\r\n");
        return hr;
    }
    return S_OK;
}

HRESULT
PasswordEncryptedStream::OpenToEncrypt(const std::wstring& pwd, const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;

    m_bEncrypting = true;
    m_pChainedStream = pChainedStream;

    if (FAILED(hr = GetKeyMaterial(pwd)))
        return hr;

    return S_OK;
}

HRESULT
PasswordEncryptedStream::OpenToDecrypt(const std::wstring& pwd, const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;
    m_bEncrypting = false;

    m_pChainedStream = pChainedStream;

    if (FAILED(hr = GetKeyMaterial(pwd)))
        return hr;

    return S_OK;
}

HRESULT PasswordEncryptedStream::EncryptData(CBinaryBuffer& pData, BOOL bFinal, DWORD& dwEncryptedBytes)
{
    HRESULT hr = E_FAIL;
    if (m_hCryptProv == NULL || m_hKey == NULL)
    {
        log::Error(_L_, E_UNEXPECTED, L"No key to encrypt with\r\n");
        return E_UNEXPECTED;
    }

    if (dwEncryptedBytes == 0)
    {
        // Nothing to encrypt
        return S_OK;
    }

    // simple case, bytes to encrypt aligned with block len
    DWORD dwDataLen = dwEncryptedBytes;

    if (!bFinal)
    {
        if (dwEncryptedBytes % m_dwBlockLen != 0)
        {
            log::Error(
                _L_,
                hr = NTE_BAD_DATA,
                L"Blocks to encrypt must be aligned on cipher block boundaries (data is %d bytes, block size is "
                L"%d)\r\n",
                dwEncryptedBytes,
                m_dwBlockLen);
            return hr;
        }
    }

    if (!CryptEncrypt(m_hKey, NULL, bFinal, 0L, pData.GetData(), &dwDataLen, (DWORD)pData.GetCount()))
    {
        if (GetLastError() == ERROR_MORE_DATA)
        {
            if (!pData.CheckCount(dwDataLen))
                return E_OUTOFMEMORY;
            dwDataLen = dwEncryptedBytes;
            if (!CryptEncrypt(m_hKey, NULL, bFinal, 0L, pData.GetData(), &dwDataLen, (DWORD)pData.GetCount()))
            {
                log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptEncrypt failed!\r\n");
                return hr;
            }
        }
        else
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptEncrypt failed!\r\n");
            return hr;
        }
    }

    _ASSERT(bFinal ? true : dwDataLen == (dwEncryptedBytes / m_dwBlockLen) * m_dwBlockLen);

    dwEncryptedBytes = dwDataLen;
    return S_OK;
}

HRESULT PasswordEncryptedStream::DecryptData(CBinaryBuffer& pData, BOOL bFinal, DWORD& dwDecryptedBytes)
{
    HRESULT hr = E_FAIL;
    if (m_hCryptProv == NULL || m_hKey == NULL)
    {
        log::Error(_L_, E_UNEXPECTED, L"No key to encrypt with\r\n");
        return E_UNEXPECTED;
    }

    if (dwDecryptedBytes == 0)
    {
        // Nothing to decrypt
        return S_OK;
    }

    DWORD dwToDecrypt = bFinal ? dwDecryptedBytes : (dwDecryptedBytes / m_dwBlockLen) * m_dwBlockLen;

    dwDecryptedBytes = 0L;

    if (!CryptDecrypt(m_hKey, NULL, bFinal, 0L, pData.GetData(), &dwToDecrypt))
    {
        log::Error(
            _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to decrypt data (%d bytes)\r\n", dwDecryptedBytes);
        return hr;
    }
    dwDecryptedBytes = dwToDecrypt;
    return S_OK;
}

HRESULT PasswordEncryptedStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;
    if (m_pChainedStream == nullptr)
        return E_POINTER;

    if (m_pChainedStream->CanRead() != S_OK)
        return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);

    if (pReadBuffer == NULL)
        return E_INVALIDARG;
    if (cbBytes > MAXDWORD)
        return E_INVALIDARG;
    if (pcbBytesRead == NULL)
        return E_POINTER;

    *pcbBytesRead = 0;

    ULONGLONG cbBytesRead = 0L;
    if (FAILED(hr = m_pChainedStream->Read(pReadBuffer, cbBytes, &cbBytesRead)))
        return hr;

    if (cbBytesRead > 0)
    {
        CBinaryBuffer buffer((LPBYTE)pReadBuffer, (size_t)cbBytes);
        DWORD dwBytesDecrypted = (DWORD)cbBytesRead;

        BOOL bFinal = cbBytesRead < cbBytes;

        if (m_bEncrypting)
        {
            if (FAILED(hr = EncryptData(buffer, bFinal, dwBytesDecrypted)))
                return hr;
        }
        else if (!m_bEncrypting)
        {
            if (FAILED(hr = DecryptData(buffer, bFinal, dwBytesDecrypted)))
                return hr;
        }
        else
        {
            log::Verbose(_L_, L"PasswordEncryptedStream is not configured!\r\n");
        }

        *pcbBytesRead = dwBytesDecrypted;

        if (dwBytesDecrypted < cbBytesRead && !bFinal)
        {
            if (FAILED(hr = m_pChainedStream->SetFilePointer(dwBytesDecrypted - cbBytesRead, FILE_CURRENT, NULL)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to SetFilePointer according to left overs (%d bytes)\r\n",
                    dwBytesDecrypted - cbBytesRead);
                return hr;
            }
        }
    }
    else
    {
        *pcbBytesRead = 0L;
    }

    return S_OK;
}

HRESULT PasswordEncryptedStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (pcbBytesWritten)
        *pcbBytesWritten = 0;

    if (m_pChainedStream == nullptr)
        return E_POINTER;
    if (CanWrite() != S_OK)
        return E_NOTIMPL;
    if (pWriteBuffer == NULL)
        return E_POINTER;

    DWORD dwLeftOvers = m_dwBufferedData % m_dwBlockLen;
    DWORD dwToProcess = m_dwBufferedData - (m_dwBufferedData % m_dwBlockLen);

    if (m_dwBufferedData > 0L)
    {
        if (m_bEncrypting)
        {

            if (FAILED(hr = EncryptData(m_Buffer, FALSE, dwToProcess)))
            {
                log::Error(_L_, hr, L"Failed to encrypt data (%d bytes)\r\n", dwToProcess);
                return hr;
            }
        }
        else if (!m_bEncrypting)
        {
            if (FAILED(hr = DecryptData(m_Buffer, FALSE, dwToProcess)))
            {
                log::Error(_L_, hr, L"Failed to decrypt data (%d bytes)\r\n", dwToProcess);
                return hr;
            }
        }
        else
        {
            log::Verbose(_L_, L"PasswordEncryptedStream is not configured!\r\n");
        }

        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = m_pChainedStream->Write(m_Buffer.GetData(), dwToProcess, &ullWritten)))
        {
            log::Error(_L_, hr, L"Failed to write encrypted data (%d bytes)\r\n", dwToProcess);
            return hr;
        }
        _ASSERT(ullWritten == dwToProcess);
    }

    _ASSERT(dwToProcess + dwLeftOvers == m_dwBufferedData);

    if (!m_Buffer.CheckCount((size_t)cbBytesToWrite + dwLeftOvers))
        return E_OUTOFMEMORY;

    if (dwLeftOvers > 0)
        MoveMemory(m_Buffer.GetData(), m_Buffer.GetData() + dwToProcess, dwLeftOvers);

    CopyMemory(m_Buffer.GetData() + dwLeftOvers, pWriteBuffer, (size_t)cbBytesToWrite);
    m_dwBufferedData = (DWORD)cbBytesToWrite + dwLeftOvers;

    if (pcbBytesWritten)
        *pcbBytesWritten = cbBytesToWrite;

    return S_OK;
}

HRESULT PasswordEncryptedStream::SetFilePointer(
    __in LONGLONG DistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pCurrPointer)
{
    if (m_pChainedStream == nullptr)
        return E_POINTER;
    return m_pChainedStream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);
}

HRESULT PasswordEncryptedStream::Close()
{
    HRESULT hr = E_FAIL;

    if (m_dwBufferedData > 0)
    {
        DWORD dwToProcess = m_dwBufferedData;
        if (m_bEncrypting)
        {
            if (FAILED(hr = EncryptData(m_Buffer, TRUE, dwToProcess)))
            {
                log::Error(_L_, hr, L"Failed to encrypt data (%d bytes)\r\n", dwToProcess);
                return hr;
            }
        }
        else if (!m_bEncrypting)
        {
            if (FAILED(hr = DecryptData(m_Buffer, TRUE, dwToProcess)))
            {
                log::Error(_L_, hr, L"Failed to decrypt data (%d bytes)\r\n", dwToProcess);
                return hr;
            }
        }
        else
        {
            log::Verbose(_L_, L"PasswordEncryptedStream is not configured!\r\n");
        }

        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = m_pChainedStream->Write(m_Buffer.GetData(), dwToProcess, &ullWritten)))
        {
            log::Error(_L_, hr, L"Failed to write encrypted data (%d bytes)\r\n", dwToProcess);
            return hr;
        }
        _ASSERT(ullWritten == dwToProcess);
        return m_pChainedStream->Close();
    }

    return S_OK;
}

PasswordEncryptedStream::~PasswordEncryptedStream()
{
    if (m_hKey != NULL)
    {
        CryptDestroyKey(m_hKey);
    }
    if (m_hCryptProv != NULL)
    {
        CryptReleaseContext(m_hCryptProv, 0L);
    }
}
