//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <Windows.h>

#include <concrt.h>

#include "OrcLib.h"

#include "BinaryBuffer.h"

#include <algorithm>

#pragma managed(push, off)

namespace Orc {

namespace TableOutput::CSV {
class Cruncher;
class FileReader;
}  // namespace TableOutput::CSV
class CircularStream;

class ORCLIB_API CircularStorage
{

    friend class TableOutput::CSV::Cruncher;
    friend class TableOutput::CSV::FileReader;
    friend class CircularStream;

private:
    size_t m_dwMaximumSize;
    size_t m_dwReallocIncrement;
    size_t m_dwInitialReservedSize;
    size_t m_dwInitialSize;

    LPBYTE m_pStore;

    LPBYTE m_pTop;
    LPBYTE m_pCursor;
    LPBYTE m_pCursorEnd;
    LPBYTE m_pBottom;

    bool m_bIsLocked;

    Concurrency::critical_section m_cs;

    // Taille d'une page (en octets)

    size_t m_dwPageSize;
    size_t PageSize() { return m_dwPageSize; }

    // Adresse de base de la page contenant l'adresse cell
    LPBYTE GetPageBaseAddress(LPBYTE cell) { return (LPBYTE)(((UINT_PTR)cell) - (((UINT_PTR)cell) % m_dwPageSize)); }

    // Adresse de base de la page suivant celle l'adresse cell
    LPBYTE GetNextPageBaseAddress(LPBYTE cell) { return GetPageBaseAddress(cell) + m_dwPageSize; }

    HRESULT CheckSpareRoomAndLock(size_t dwSafeSizeAtLock)
    {
        // check if we have dwSafeSizeAtLock bytes free before locking.

        if ((size_t)(((UINT_PTR)m_pStore + m_dwMaximumSize) - (UINT_PTR)m_pTop) < dwSafeSizeAtLock && !m_bIsLocked)
        {
            // We need to wrap up the buffer as there is not enough space left at the end of the store to be "safe"

            size_t NbBytesToSave = (size_t)(m_pCursorEnd - m_pBottom);

            size_t NewWindowsSize = (size_t)(NbBytesToSave + dwSafeSizeAtLock);

            if (NewWindowsSize > MAXLONG)
            {
                // WAYYYY too big window...
                return E_INVALIDARG;
            }
            if (NewWindowsSize > m_dwMaximumSize)
            {
                // We are full NbByteToSave+SafeSize > MaximumSize....
                // We are going to try re-allocate the buffer
                size_t dwNewSize = std::max(NewWindowsSize, m_dwMaximumSize + m_dwReallocIncrement);

                LPBYTE pNewStore = (LPBYTE)VirtualAlloc(m_pStore, dwNewSize, MEM_RESERVE, PAGE_READWRITE);

                if (pNewStore == NULL)
                {
                    // failed to expand our region, now try to relocate our buffer somewhere else...
                    pNewStore = (LPBYTE)VirtualAlloc(NULL, dwNewSize, MEM_RESERVE, PAGE_READWRITE);
                    if (pNewStore == NULL)
                        return E_OUTOFMEMORY;

                    // TODO: to decrease memory pressure, we should VirtualAlloc(MEM_COMMIT) -> MoveMemory ->
                    // VirtualAlloc(MEM_DECOMIT) by chunks

                    if (VirtualAlloc(pNewStore, (SIZE_T)NewWindowsSize + sizeof(BYTE), MEM_COMMIT, PAGE_READWRITE)
                        == NULL)
                        return HRESULT_FROM_WIN32(GetLastError());

                    MoveMemory(pNewStore, m_pBottom, NbBytesToSave);

                    if (!VirtualFree(m_pStore, 0L, MEM_RELEASE))
                        return HRESULT_FROM_WIN32(GetLastError());

                    // CursorOffset is the size from Bottom to Cursor that gets "wrapped" to the start address of the
                    // store.
                    m_pStore = pNewStore;
                    size_t CursorOffset = (size_t)(m_pCursor - m_pBottom);
                    m_pCursor = m_pStore + CursorOffset;
                    m_pCursorEnd = m_pStore + NbBytesToSave;

                    m_pBottom = m_pStore;
                    m_pTop = GetNextPageBaseAddress(m_pStore + NewWindowsSize) - sizeof(BYTE);
                    *m_pTop = 0;
                }
            }
            else
            {
                // we have enough space free in the store, wrapping up the buffer

                if (VirtualAlloc(m_pStore, (SIZE_T)NewWindowsSize + sizeof(BYTE), MEM_COMMIT, PAGE_READWRITE) == NULL)
                    return HRESULT_FROM_WIN32(GetLastError());

                MoveMemory(m_pStore, m_pBottom, NbBytesToSave);

                LPBYTE pFirstByteToDecommit = GetPageBaseAddress(m_pStore + NewWindowsSize) + PageSize();
                size_t SizeToDecommit = (size_t)((m_pStore + m_dwMaximumSize) - pFirstByteToDecommit);

                if (SizeToDecommit > PageSize())
                {
                    if (!VirtualFree(pFirstByteToDecommit, (size_t)SizeToDecommit, MEM_DECOMMIT))
                        return HRESULT_FROM_WIN32(GetLastError());
                }

                // CursorOffset is the size from Bottom to Cursor that gets "wrapped" to the start address of the store.
                size_t CursorOffset = (size_t)(m_pCursor - m_pBottom);

                m_pCursor = m_pStore + CursorOffset;
                m_pCursorEnd = m_pStore + NbBytesToSave;

                m_pBottom = m_pStore;
                m_pTop = GetNextPageBaseAddress(m_pStore + NewWindowsSize) - sizeof(BYTE);
                *m_pTop = 0;
            }
        }
        else if ((size_t)(m_pStore + m_dwMaximumSize - m_pTop) < dwSafeSizeAtLock)
        {
            // Buffer is Locked and we don't have enough "safe" size
            return E_OUTOFMEMORY;
        }
        else if ((size_t)(m_pTop - m_pCursorEnd) < dwSafeSizeAtLock)
        {
            LPBYTE pCommitted =
                (LPBYTE)VirtualAlloc(m_pCursorEnd, dwSafeSizeAtLock + sizeof(BYTE), MEM_COMMIT, PAGE_READWRITE);
            // we just need to commit the necessary safe size after the current top
            if (pCommitted == NULL)
                return HRESULT_FROM_WIN32(GetLastError());

            m_pTop = GetNextPageBaseAddress(m_pCursorEnd + dwSafeSizeAtLock) - sizeof(BYTE);
            *m_pTop = 0;
        }

        m_bIsLocked = true;
        return S_OK;
    }

    HRESULT Unlock()
    {
        m_bIsLocked = false;
        return S_OK;
    }

    HRESULT SetNewBottom(LPBYTE pNewBottom)
    {
        if (pNewBottom != NULL)
        {
            if (pNewBottom < m_pBottom)
                return E_INVALIDARG;
            if (pNewBottom == m_pBottom)
                return S_OK;
            if (GetPageBaseAddress(pNewBottom) == GetPageBaseAddress(m_pBottom))
                return S_OK;

            LPBYTE pFirstByteToFree = GetPageBaseAddress(m_pBottom);
            LPBYTE pLastByteToFree = GetPageBaseAddress(pNewBottom) - sizeof(BYTE);

            if ((size_t)(pLastByteToFree - pFirstByteToFree) > PageSize())
            {
                if (!VirtualFree(pFirstByteToFree, pLastByteToFree - pFirstByteToFree, MEM_DECOMMIT))
                    return HRESULT_FROM_WIN32(GetLastError());
            }
            m_pBottom = GetPageBaseAddress(pNewBottom);
        }
        return S_OK;
    }

    HRESULT ForwardCursorBy(size_t dwIncrement = 1)
    {
        if (m_pCursor + dwIncrement > m_pTop)
        {
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        }
        if (m_pCursor + dwIncrement > m_pCursorEnd)
        {
            m_pCursor += dwIncrement;
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        }
        m_pCursor += dwIncrement;
        return S_OK;
    }

    HRESULT ResetCursor(LPBYTE pNewCursor)
    {
        if (pNewCursor < m_pBottom || pNewCursor > m_pCursorEnd)
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        m_pCursor = pNewCursor;
        m_pBottom = GetPageBaseAddress(m_pCursor);
        return S_OK;
    }

    LPBYTE Cursor() { return m_pCursor; }

    size_t SizeOfCursor() { return (size_t)(m_pCursorEnd - m_pCursor); }

    HRESULT ForwardEndOfCursorBy(size_t dwIncrement)
    {
        if (m_pCursorEnd + dwIncrement > m_pTop)
        {
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        }
        m_pCursorEnd += dwIncrement;
        return S_OK;
    }

    LPBYTE EndOfCursor() { return m_pCursorEnd; }

public:
    CircularStorage()
    {
        m_dwPageSize = 0;
        m_bIsLocked = false;

        m_dwMaximumSize = 0;
        m_dwInitialSize = 0;
        m_dwReallocIncrement = 0;
        m_dwInitialReservedSize = 0;

        m_pStore = NULL;  // (le pointeur de base de la zone réservée

        m_pTop = NULL;  // (le "haut" de la zone commitée)
        m_pCursor = NULL;  // (curseur actuel)
        m_pCursorEnd = NULL;  // (la position de la dernière donnée valide )
        m_pBottom = NULL;  // (le "bas" de la zone commitée )
    }

    HRESULT InitializeStorage(DWORD dwMaximumSize, DWORD dwInitialSize)
    {
        if (!m_dwPageSize)
        {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            m_dwPageSize = sysinfo.dwPageSize;
        }
        m_dwMaximumSize = dwMaximumSize;
        m_dwInitialSize = dwInitialSize;

        if (m_pStore != NULL)
        {
            if (!VirtualFree(m_pStore, 0, MEM_RELEASE))
                return HRESULT_FROM_WIN32(GetLastError());
        }
        // réservation de l'espace nécessaire au stockage
        m_pStore = (LPBYTE)VirtualAlloc(NULL, m_dwMaximumSize, MEM_RESERVE, PAGE_READWRITE);
        if (m_pStore == NULL)
            return E_OUTOFMEMORY;

        if (VirtualAlloc(m_pStore, m_dwInitialSize, MEM_COMMIT, PAGE_READWRITE) == NULL)
            return E_OUTOFMEMORY;

        m_pTop = GetNextPageBaseAddress(m_pStore + m_dwInitialSize - sizeof(BYTE)) - sizeof(BYTE);
        *m_pTop = 0;
        m_pBottom = m_pStore;
        m_pCursor = m_pStore;
        m_pCursorEnd = m_pStore;
        return S_OK;
    }

    HRESULT IsInitialized()
    {
        if (m_pStore == NULL)
            return S_FALSE;
        else
            return S_OK;
    };

    size_t GetMaximumSize() { return m_dwMaximumSize; };
    size_t GetAvailableSize() { return m_dwMaximumSize - (size_t)(m_pCursorEnd - m_pBottom); };

    HRESULT PushBytes(const CBinaryBuffer& buffer)
    {
        HRESULT hr = E_FAIL;

        if (FAILED(hr = CheckSpareRoomAndLock(buffer.GetCount())))
        {
            // Not enough space
            Unlock();
            return hr;
        }

        CopyMemory(EndOfCursor(), buffer.GetData(), buffer.GetCount());
        ForwardEndOfCursorBy(buffer.GetCount());
        Unlock();
        return S_OK;
    }
    HRESULT PushBytes(LPBYTE pData, size_t cbLen)
    {
        HRESULT hr = E_FAIL;

        if (FAILED(hr = CheckSpareRoomAndLock(cbLen)))
        {
            // Not enough space
            Unlock();
            return hr;
        }

        CopyMemory(EndOfCursor(), pData, cbLen);
        ForwardEndOfCursorBy(cbLen);
        Unlock();
        return S_OK;
    }

    HRESULT PopBytes(CBinaryBuffer& buffer, size_t cbLen)
    {
        if (!buffer.SetCount(cbLen))
            return E_OUTOFMEMORY;
        if (buffer.GetCount() != cbLen)
            return E_OUTOFMEMORY;

        size_t cbBytesToCopy = std::min(cbLen, SizeOfCursor());

        CopyMemory(buffer.GetData(), Cursor(), cbBytesToCopy);

        ForwardCursorBy(cbBytesToCopy);
        SetNewBottom(Cursor());
        return S_OK;
    }

    HRESULT ClearStorage()
    {
        HRESULT hr = E_FAIL;
        if (FAILED(hr = SetNewBottom(m_pStore)))
            return hr;

        if (VirtualFree(m_pStore, m_dwMaximumSize, MEM_DECOMMIT) == NULL)
            return HRESULT_FROM_WIN32(GetLastError());

        if (VirtualAlloc(m_pStore, m_dwInitialSize, MEM_COMMIT, PAGE_READWRITE) == NULL)
            return E_OUTOFMEMORY;

        m_pTop = GetNextPageBaseAddress(m_pStore + m_dwInitialSize) - sizeof(BYTE);
        m_pBottom = m_pStore;
        m_pCursor = m_pStore;
        m_pCursorEnd = m_pStore;
        return S_OK;
    }
    HRESULT ClearCursor()
    {
        m_pCursorEnd = m_pCursor;
        m_pTop = GetNextPageBaseAddress(m_pCursor) - sizeof(BYTE);
        return S_OK;
    }
    size_t Size() { return (size_t)(m_pCursorEnd - m_pCursor); }

    HRESULT UninitializeStorage()
    {
        m_dwPageSize = 0L;
        m_dwMaximumSize = 0L;
        m_dwInitialSize = 0L;

        if (m_pStore != NULL)
        {
            VirtualFree(m_pStore, 0, MEM_RELEASE);
            m_pStore = NULL;
        }

        m_pTop = m_pStore;
        m_pBottom = m_pStore;
        m_pCursor = m_pStore;
        m_pCursorEnd = m_pStore;
        return S_OK;
    }

    ~CircularStorage()
    {
        if (m_pStore)
            VirtualFree(m_pStore, 0L, MEM_RELEASE);
        m_pStore = NULL;
    }
};
}  // namespace Orc

#pragma managed(pop)
