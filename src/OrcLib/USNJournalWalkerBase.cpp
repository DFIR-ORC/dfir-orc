//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "USNJournalWalkerBase.h"
#include "MountedVolumeReader.h"

using namespace Orc;

USNJournalWalkerBase::USNJournalWalkerBase()
    : m_RecordStore(L"USNRecordStore")
{
    m_dwlRootUSN = 0;
    m_cchMaxComponentLength = 0;
    m_dwRecordMaxSize = 0;

    m_dwWalkedItems = 0;

    m_pFullNameBuffer = NULL;
    m_cbFullNameBufferLen = 0L;
}

USNJournalWalkerBase::~USNJournalWalkerBase()
{
    if (m_pFullNameBuffer)
        HeapFree(GetProcessHeap(), 0L, m_pFullNameBuffer);
}

const USNJournalWalkerBase::USN_MAP& USNJournalWalkerBase::GetUSNMap()
{
    return m_USNMap;
}

HRESULT USNJournalWalkerBase::ExtendNameBuffer(WCHAR** pCurrent)
{
    WCHAR* pNewBuf = NULL;

    pNewBuf =
        (WCHAR*)HeapAlloc(GetProcessHeap(), 0L, m_cbFullNameBufferLen + (m_cchMaxComponentLength * sizeof(WCHAR)));
    if (!pNewBuf)
        return E_OUTOFMEMORY;

    if (m_pFullNameBuffer)
    {
#ifdef DEBUG
        wmemset(pNewBuf, L'#', (m_cbFullNameBufferLen + (m_cchMaxComponentLength * sizeof(WCHAR))) / sizeof(WCHAR));
        pNewBuf[(m_cbFullNameBufferLen + (m_cchMaxComponentLength * sizeof(WCHAR))) / sizeof(WCHAR) - 1] = 0;
#endif
        wmemcpy_s(
            pNewBuf + m_cchMaxComponentLength - 1,
            m_cbFullNameBufferLen / sizeof(WCHAR) + m_cchMaxComponentLength,
            m_pFullNameBuffer,
            m_cbFullNameBufferLen / sizeof(WCHAR));
    }

    if (pCurrent)
    {
        DWORD dwIndex = (DWORD)(*pCurrent - m_pFullNameBuffer);

        *pCurrent = pNewBuf + (m_cchMaxComponentLength)-1;
        *pCurrent += dwIndex;
        _ASSERT(
            *pCurrent
            <= (pNewBuf + (m_cbFullNameBufferLen + (m_cchMaxComponentLength * sizeof(WCHAR))) / sizeof(WCHAR)));
    }

    if (m_pFullNameBuffer)
        HeapFree(GetProcessHeap(), 0L, m_pFullNameBuffer);

    m_pFullNameBuffer = pNewBuf;
    m_cbFullNameBufferLen += m_cchMaxComponentLength * sizeof(WCHAR);

    return S_OK;
}

WCHAR* USNJournalWalkerBase::GetFullNameAndIfInLocation(USN_RECORD* pElt, DWORD* pdwLen, bool* pbInSpecificLocation)
{
    using namespace msl::utilities;

    WCHAR* pCurrent = NULL;

    if (pbInSpecificLocation)
    {
        *pbInSpecificLocation = false;

        if (m_LocationsRefNum.empty())
            *pbInSpecificLocation = true;
    }

    if (m_pFullNameBuffer == NULL)
    {
        if (m_cbFullNameBufferLen == 0)
            m_cbFullNameBufferLen = m_cchMaxComponentLength * 2;
        m_pFullNameBuffer = (WCHAR*)HeapAlloc(GetProcessHeap(), 0L, SafeInt<USHORT>(m_cchMaxComponentLength) * 2);
        if (!m_pFullNameBuffer)
            return NULL;
    }
#ifdef DEBUG
    wmemset(m_pFullNameBuffer, L'_', m_cchMaxComponentLength / sizeof(WCHAR));
    m_pFullNameBuffer[m_cchMaxComponentLength / sizeof(WCHAR) - 1] = 0;
#endif

    DWORD dwCount = 0;
    auto pParentPair = m_USNMap.find(pElt->ParentFileReferenceNumber);

    DWORDLONG dwlLastParentRefNumber = 0;

    if (dwCount > m_cbFullNameBufferLen)
    {
        if (FAILED(ExtendNameBuffer(&pCurrent)))
            return NULL;
    }

    {
        dwCount += sizeof(WCHAR);
        if (dwCount > m_cbFullNameBufferLen)
        {
            if (FAILED(ExtendNameBuffer(&pCurrent)))
                return NULL;
        }
        pCurrent = m_pFullNameBuffer + (m_cbFullNameBufferLen / sizeof(WCHAR)) - 1;
        *pCurrent = 0;
    }
    {
        dwCount += pElt->FileNameLength;
        if (dwCount > m_cbFullNameBufferLen)
        {
            if (FAILED(ExtendNameBuffer(&pCurrent)))
                return NULL;
        }
        pCurrent -= pElt->FileNameLength / sizeof(WCHAR);
        memcpy_s(pCurrent, dwCount, pElt->FileName, pElt->FileNameLength);
    }

    if (pbInSpecificLocation)
        if (!*pbInSpecificLocation)
        {
            *pbInSpecificLocation =
                (m_LocationsRefNum.find(pElt->FileReferenceNumber) != m_LocationsRefNum.end()) ? true : false;
            if (!*pbInSpecificLocation)
                *pbInSpecificLocation =
                    (m_LocationsRefNum.find(pElt->ParentFileReferenceNumber) != m_LocationsRefNum.end()) ? true : false;
        }

    dwlLastParentRefNumber = pElt->ParentFileReferenceNumber;
    while (pParentPair != m_USNMap.end())
    {
        {
            dwCount += sizeof(WCHAR);
            if (dwCount > m_cbFullNameBufferLen)
            {
                if (FAILED(ExtendNameBuffer(&pCurrent)))
                    return NULL;
            }
            pCurrent--;
            *pCurrent = L'\\';
        }
        {
            dwCount += pParentPair->second->FileNameLength;
            if (dwCount > m_cbFullNameBufferLen)
            {
                if (FAILED(ExtendNameBuffer(&pCurrent)))
                    return NULL;
            }
            pCurrent -= pParentPair->second->FileNameLength / sizeof(WCHAR);
            memcpy_s(pCurrent, dwCount, pParentPair->second->FileName, pParentPair->second->FileNameLength);
        }
        dwlLastParentRefNumber = pParentPair->second->ParentFileReferenceNumber;
        pParentPair = m_USNMap.find(pParentPair->second->ParentFileReferenceNumber);

        if (pbInSpecificLocation)
            if (!*pbInSpecificLocation)
                *pbInSpecificLocation =
                    (m_LocationsRefNum.find(dwlLastParentRefNumber) != m_LocationsRefNum.end()) ? true : false;
    }
    if (dwlLastParentRefNumber == m_dwlRootUSN)
    {
        DWORD dwVolLen = (DWORD)wcslen(m_VolReader->ShortVolumeName());
        dwCount += dwVolLen * sizeof(WCHAR);
        if (dwCount > m_cbFullNameBufferLen)
        {
            if (FAILED(ExtendNameBuffer(&pCurrent)))
                return NULL;
        }
        pCurrent -= dwVolLen;
        memcpy_s(pCurrent, dwCount, m_VolReader->ShortVolumeName(), dwVolLen * sizeof(WCHAR));

        if (pdwLen)
            *pdwLen = dwCount;
        return pCurrent;
    }
    else
    {
        // Parent folder was _not_ found, inserting "place holder"
        // Adding \\ (i.e. one backslash)

        {
            dwCount += sizeof(WCHAR);
            if (dwCount > m_cbFullNameBufferLen)
            {
                if (FAILED(ExtendNameBuffer(&pCurrent)))
                    return NULL;
            }
            _ASSERT(dwCount <= m_cbFullNameBufferLen);

            pCurrent--;
            _ASSERT(pCurrent >= m_pFullNameBuffer);
            *pCurrent = L'\\';
        }

        {
            dwCount += sizeof(WCHAR) * 20;
            if (dwCount > m_cbFullNameBufferLen)
            {
                if (FAILED(ExtendNameBuffer(&pCurrent)))
                    return NULL;
            }
            _ASSERT(dwCount <= m_cbFullNameBufferLen);

            pCurrent -= 20;
            swprintf_s(pCurrent, 21, L"__%.16I64X__", dwlLastParentRefNumber);
            pCurrent[20] = L'\\';
        }

        {
            dwCount += sizeof(WCHAR);
            if (dwCount > m_cbFullNameBufferLen)
            {
                if (FAILED(ExtendNameBuffer(&pCurrent)))
                    return NULL;
            }
            _ASSERT(dwCount <= m_cbFullNameBufferLen);

            pCurrent--;
            _ASSERT(pCurrent >= m_pFullNameBuffer);
            *pCurrent = L'\\';
        }

        if (pdwLen)
            *pdwLen = dwCount;
        return pCurrent;
    }
}
