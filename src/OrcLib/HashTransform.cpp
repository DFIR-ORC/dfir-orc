//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"
#include "HashTransform.h"

#include "CryptoUtilities.h"

using namespace Orc;

HCRYPTPROV HashTransform::g_hProv = NULL;

HashTransform::HashTransform(
    Concurrency::ITarget<CBinaryBuffer>& md5Target,
    Concurrency::ITarget<CBinaryBuffer>& sha1Target)
    : m_md5Target(md5Target)
    , m_sha1Target(sha1Target)
    , m_hMD5(NULL)
    , m_hSha1(NULL)
{
    // ResetHash(true);
}

HRESULT HashTransform::ResetHash(bool bContinue)
{
    if (m_hMD5 != NULL)
        CryptDestroyHash(m_hMD5);
    if (m_hSha1 != NULL)
        CryptDestroyHash(m_hSha1);

    m_hMD5 = m_hSha1 = NULL;
    m_bHashIsValid = false;

    if (bContinue)
    {
        if (g_hProv == NULL)
            if (!CryptAcquireContext(&g_hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
                return HRESULT_FROM_WIN32(GetLastError());

        m_bHashIsValid = true;
        if (!CryptCreateHash(g_hProv, CALG_MD5, 0, 0, &m_hMD5))
            return HRESULT_FROM_WIN32(GetLastError());
        if (!CryptCreateHash(g_hProv, CALG_SHA1, 0, 0, &m_hSha1))
            return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

HRESULT HashTransform::HashData(LPBYTE pBuffer, DWORD dwBytesToHash)
{
    if (m_bHashIsValid)
    {
        if (m_hMD5)
            if (!CryptHashData(m_hMD5, pBuffer, (DWORD)dwBytesToHash, 0))
                return HRESULT_FROM_WIN32(GetLastError());
        if (m_hSha1)
            if (!CryptHashData(m_hSha1, pBuffer, (DWORD)dwBytesToHash, 0))
                return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

std::shared_ptr<StreamMessage> HashTransform::operator()(const std::shared_ptr<StreamMessage>& item)
{
    if (m_hMD5 == NULL || m_hSha1 == NULL)
        ResetHash(true);

    if (FAILED(item->HResult()))
        return item;

    if (item->Request() == StreamMessage::Read || item->Request() == StreamMessage::Write)
    {
        item->SetHResult(HashData(item->Buffer().GetData(), (DWORD)item->Buffer().GetCount()));
    }

    if (item->Request() == StreamMessage::Close)
    {
        CBinaryBuffer MD5, SHA1;
        if (m_bHashIsValid)
        {
            DWORD cbHash = BYTES_IN_MD5_HASH;
            MD5.SetCount(BYTES_IN_MD5_HASH);
            if (!CryptGetHashParam(m_hMD5, HP_HASHVAL, MD5.GetData(), &cbHash, 0))
            {
                item->SetHResult(HRESULT_FROM_WIN32(GetLastError()));
            }
            else
                item->SetHResult(S_OK);

            cbHash = BYTES_IN_SHA1_HASH;
            SHA1.SetCount(BYTES_IN_SHA1_HASH);
            if (!CryptGetHashParam(m_hSha1, HP_HASHVAL, SHA1.GetData(), &cbHash, 0))
            {
                item->SetHResult(HRESULT_FROM_WIN32(GetLastError()));
            }
            else
                item->SetHResult(S_OK);
        }
        else
        {
            MD5.SetCount(0);
            SHA1.SetCount(0);
            item->SetHResult(S_OK);
        }

        send(m_md5Target, MD5);
        send(m_sha1Target, SHA1);
    }
    return item;
}

HashTransform::~HashTransform(void)
{
    if (m_hMD5 != NULL)
    {
        CryptDestroyHash(m_hMD5);
        m_hMD5 = NULL;
    }
    if (m_hSha1 != NULL)
    {
        CryptDestroyHash(m_hSha1);
        m_hSha1 = NULL;
        ;
    }
}
