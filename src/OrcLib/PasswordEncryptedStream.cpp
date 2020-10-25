//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "PasswordEncryptedStream.h"

#include "CryptoUtilities.h"

#include <boost/scope_exit.hpp>

using namespace Orc;

constexpr auto ENCRYPT_ALGORITHM = CALG_AES_256;

HRESULT PasswordEncryptedStream::GetKeyMaterial(const std::wstring& pwd)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CryptoUtilities::AcquireContext(m_hCryptProv)))
    {
        Log::Error("Failed to CryptAcquireContext [{}]", SystemError(hr));
        return hr;
    }

    HCRYPTHASH hHash = NULL;
    if (!CryptCreateHash(m_hCryptProv, CALG_SHA1, 0, 0, &hHash))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CryptCreateHash [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT(hHash) { CryptDestroyHash(hHash); }
    BOOST_SCOPE_EXIT_END;

    if (!CryptHashData(hHash, (BYTE*)pwd.c_str(), (DWORD)pwd.size() * sizeof(WCHAR), 0))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed CryptHashData [{}]", SystemError(hr));
        return hr;
    }

    if (!CryptDeriveKey(m_hCryptProv, ENCRYPT_ALGORITHM, hHash, 0L, &m_hKey))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed CryptDeriveKey [{}]", SystemError(hr));
        return hr;
    }

    DWORD dwDataLen = sizeof(DWORD);
    if (!CryptGetKeyParam(m_hKey, KP_BLOCKLEN, (BYTE*)&m_dwBlockLen, &dwDataLen, 0L))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed CryptGetKeyParam [{}]", SystemError(hr));
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
        Log::Error("EncryptData: No key to encrypt with");
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
            Log::Error(
                "Blocks to encrypt must be aligned on cipher block boundaries (data is {} bytes, block size is {})",
                dwEncryptedBytes,
                m_dwBlockLen);
            return NTE_BAD_DATA;
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
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Error("Failed CryptEncrypt [{}]", SystemError(hr));
                return hr;
            }
        }
        else
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed CryptEncrypt [{}]", SystemError(hr));
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
        Log::Error("DecryptData: No key to decrypt with");
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
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to decrypt data ({} bytes, [{}])", dwDecryptedBytes, SystemError(hr));
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
            Log::Debug(L"PasswordEncryptedStream is not configured!");
        }

        *pcbBytesRead = dwBytesDecrypted;

        if (dwBytesDecrypted < cbBytesRead && !bFinal)
        {
            if (FAILED(hr = m_pChainedStream->SetFilePointer(dwBytesDecrypted - cbBytesRead, FILE_CURRENT, NULL)))
            {
                Log::Error(
                    "Failed to SetFilePointer according to left overs ({} bytes)", dwBytesDecrypted - cbBytesRead);
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
                Log::Error("Failed to encrypt data ({} bytes, [{}])", dwToProcess, SystemError(hr));
                return hr;
            }
        }
        else if (!m_bEncrypting)
        {
            if (FAILED(hr = DecryptData(m_Buffer, FALSE, dwToProcess)))
            {
                Log::Error("Failed to decrypt data ({} bytes, [{}])", dwToProcess, SystemError(hr));
                return hr;
            }
        }
        else
        {
            Log::Debug("PasswordEncryptedStream is not configured");
        }

        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = m_pChainedStream->Write(m_Buffer.GetData(), dwToProcess, &ullWritten)))
        {
            Log::Error("Failed to write encrypted data ({} bytes, [{}])", dwToProcess, SystemError(hr));
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
                Log::Error("Failed to encrypt data ({} bytes, [{}])", dwToProcess, SystemError(hr));
                return hr;
            }
        }
        else if (!m_bEncrypting)
        {
            if (FAILED(hr = DecryptData(m_Buffer, TRUE, dwToProcess)))
            {
                Log::Error("Failed to decrypt data ({} bytes, [{}])", dwToProcess, SystemError(hr));
                return hr;
            }
        }
        else
        {
            Log::Debug("PasswordEncryptedStream is not configured");
        }

        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = m_pChainedStream->Write(m_Buffer.GetData(), dwToProcess, &ullWritten)))
        {
            Log::Error(L"Failed to write encrypted data ({} bytes, [{}])", dwToProcess, SystemError(hr));
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
