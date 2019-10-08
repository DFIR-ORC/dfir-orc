//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <functional>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API HeapStorage
{

private:
    HANDLE m_heap = NULL;
    DWORD m_dwElementSize = 0L;
    DWORD m_dwMaxObjects = 0L;
    DWORD m_NumberOfAllocatedCells = 0L;
    bool m_Initialized = false;

    const LPWSTR m_szStoreDescription;

public:
    // Constructeur du "storage" d'objets
    HeapStorage(const LPWSTR szDescription)
        : m_szStoreDescription(szDescription) {};

    HeapStorage(const DWORD dwMaxObjects, const DWORD dwElementSize, const LPWSTR szDescription)
        : m_szStoreDescription(szDescription)
    {
        InitializeStore(dwMaxObjects, dwElementSize);
    };

    HRESULT InitializeStore(const DWORD dwMaxObjects, const DWORD dwElementSize)
    {
        m_heap = HeapCreate(HEAP_NO_SERIALIZE, 0L, (size_t)dwMaxObjects * dwElementSize);
        if (m_heap == NULL)
            return HRESULT_FROM_WIN32(GetLastError());
        m_dwElementSize = dwElementSize;
        m_dwMaxObjects = dwMaxObjects;
        m_Initialized = true;
        m_NumberOfAllocatedCells = 0L;
        return S_OK;
    }

    size_t AllocatedCells() { return m_NumberOfAllocatedCells; }

    LPVOID GetNewCell()
    {

        if (!m_Initialized)
            throw "Storage is not initialized!!!";

        const auto alloc = HeapAlloc(m_heap, HEAP_ZERO_MEMORY, m_dwElementSize);
        if (alloc)
            m_NumberOfAllocatedCells++;

        return alloc;
    }

    // libération d'une cellule
    void FreeCell(LPVOID cell)
    {

        if (!m_Initialized)
            throw "Storage is not initialized!!!";

        if (cell)
        {
            m_NumberOfAllocatedCells--;
            _ASSERT(HeapFree(m_heap, 0L, cell));
        }
    }

    HRESULT EnumCells(std::function<void(void* lpData)> pCallback)
    {
        PROCESS_HEAP_ENTRY heapEntry;
        heapEntry.lpData = NULL;

        while (HeapWalk(m_heap, &heapEntry))
        {
            if (heapEntry.lpData == NULL)
                break;
            if (heapEntry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
            {
                if (heapEntry.cbData != m_dwElementSize)
                    return HRESULT_FROM_WIN32(ERROR_INVALID_BLOCK_LENGTH);
                pCallback(heapEntry.lpData);
            }
        }
        if (GetLastError() != ERROR_NO_MORE_ITEMS)
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }

    // destruction du storage on libère globalement la mémoire
    ~HeapStorage()
    {
        if (m_Initialized)
        {
            HeapDestroy(m_heap);
        }
    }
};

}  // namespace Orc
#pragma managed(pop)
