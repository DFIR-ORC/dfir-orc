//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#pragma managed(push, off)

namespace Orc {

static const DWORD g_FreePattern = 0xDDDDDDDD;
static const DWORD g_FreshPattern = 0xCCCCCCCC;

class ObjectStorage
{

private:
    bool m_Initialized;

    // Adresse de base de l'espace de stockage
    void* m_pStore;

    // Adresse de base de la "réserve" d'instances libérées
    LPVOID* m_pFreeStore;

    // Dernier item libéré
    LPVOID* m_pLastFreed;

    DWORD m_dwElementSize;
    // Nombre d'instances utilisée dans le storage
    DWORD m_dwCount;

    // Nombre maximum d'objets
    DWORD m_dwMaxObjects;

    DWORD m_dwPageSize;
    DWORD m_dwPerPageCount;
    DWORD m_dwPageCount;

    const LPWSTR m_szStoreDescription;

    // Section critique de protection
    CRITICAL_SECTION m_cs;

    // Struture de gestion d'une page (allouée "dans" la page)
    struct PageMgtBlock
    {
        CHAR m_PreTag[4];
        DWORD m_UseCount;
        CHAR m_PostTag[4];
    };

    struct PageMgtBlock m_TemplateMgtBlock;

    // Nombre d'instance par page
    DWORD PerPageCount()
    {
        if (m_dwPerPageCount)
            return m_dwPerPageCount;
        m_dwPerPageCount = (PageSize() - sizeof(PageMgtBlock)) / m_dwElementSize;
        return m_dwPerPageCount;
    }

    // Nombre de pages dans l'espace de stockage
    DWORD PageCount()
    {
        if (m_dwPageCount)
            return m_dwPageCount;
        m_dwPageCount = (m_dwMaxObjects / PerPageCount()) + 1;
        return m_dwPageCount;
    }

    // Taille d'une page (en octets)
    DWORD PageSize()
    {
        if (!m_dwPageSize)
        {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            m_dwPageSize = sysinfo.dwPageSize;
        }
        return m_dwPageSize;
    }

    // Adresse du block de gestion d'une page
    PageMgtBlock* GetPageMgtBlock(LPVOID cell)
    {
        PageMgtBlock* pBlock = (PageMgtBlock*)((LPBYTE)GetPageBaseAddress(cell) + PageSize() - sizeof(PageMgtBlock));
        CheckPageMgtBlock(cell);
        return pBlock;
    }

    void InitializePageMgtBlock(LPVOID cell)
    {
        *((PageMgtBlock*)((LPBYTE)GetPageBaseAddress(cell) + PageSize() - sizeof(PageMgtBlock))) = m_TemplateMgtBlock;
    }

    void CheckPageMgtBlock(LPVOID cell)
    {
        PageMgtBlock* pBlock = (PageMgtBlock*)((LPBYTE)GetPageBaseAddress(cell) + PageSize() - sizeof(PageMgtBlock));
        if (strncmp(m_TemplateMgtBlock.m_PreTag, pBlock->m_PreTag, 4))
            throw "ObjectStorage: MgtBlock preTag corrupted!!";
        if (strncmp(m_TemplateMgtBlock.m_PostTag, pBlock->m_PostTag, 4))
            throw "ObjectStorage: MgtBlock postTag corrupted!!";
        if (pBlock->m_UseCount > m_dwPerPageCount)
            throw "ObjectStorage: MgtBlock PerPageCount corrupted!!";
    }

    // Nombre d'instances sur la page
    DWORD* GetPageUseCount(LPVOID cell) { return &(GetPageMgtBlock(cell)->m_UseCount); }

    // Adresse de base de la page contenant l'adresse cell
    LPVOID GetPageBaseAddress(LPVOID cell) { return (LPVOID)(((UINT_PTR)cell) - ((UINT_PTR)cell) % PageSize()); }

    // Commite "physiquement" une page
    HRESULT CommitPage(LPVOID pv)
    {

        LPVOID base = GetPageBaseAddress(pv);

        if (!VirtualAlloc(base, PageSize(), MEM_COMMIT, PAGE_READWRITE))
            return HRESULT_FROM_WIN32(GetLastError());

        InitializePageMgtBlock(base);

        return S_OK;
    }

    HRESULT IsPageCommitted(LPVOID pv)
    {

        LPVOID base = GetPageBaseAddress(pv);

        MEMORY_BASIC_INFORMATION MemInfo;
        ZeroMemory(&MemInfo, sizeof(MEMORY_BASIC_INFORMATION));

        if (!VirtualQuery(base, &MemInfo, PageSize()))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        return MemInfo.State & MEM_COMMIT ? S_OK : S_FALSE;
    }

    // Initialisation d'une cellule
    HRESULT InitializeCell(LPVOID pv)
    {
#if _DEBUG
        CheckPageMgtBlock(pv);
        memset(pv, g_FreshPattern, m_dwElementSize);
#endif
        return S_OK;
    }

public:
    // Constructeur du "storage" d'objets

    ObjectStorage(const LPWSTR szDescription, const LPSTR szPreTag = "AAAA", const LPSTR szPostTag = "BBBB")
        : m_Initialized(false)
        , m_szStoreDescription(szDescription)
    {
        ZeroMemory(&m_TemplateMgtBlock, sizeof(PageMgtBlock));
        CopyMemory(m_TemplateMgtBlock.m_PreTag, szPreTag, 4);
        CopyMemory(m_TemplateMgtBlock.m_PostTag, szPostTag, 4);
    };

    ObjectStorage(
        const DWORD dwMaxObjects,
        const DWORD dwElementSize,
        const LPWSTR szDescription,
        const LPSTR szPreTag = "AAAA",
        const LPSTR szPostTag = "BBBB")
        : m_Initialized(false)
        , m_szStoreDescription(szDescription)
    {
        ZeroMemory(&m_TemplateMgtBlock, sizeof(PageMgtBlock));
        CopyMemory(m_TemplateMgtBlock.m_PreTag, szPreTag, 4);
        CopyMemory(m_TemplateMgtBlock.m_PostTag, szPostTag, 4);
        InitializeStore(dwMaxObjects, dwElementSize);
    };

    HRESULT InitializeStore(const DWORD dwMaxObjects, const DWORD dwElementSize)
    {
        InitializeCriticalSection(&m_cs);

        EnterCriticalSection(&m_cs);

        m_dwMaxObjects = dwMaxObjects;
        m_dwElementSize = dwElementSize;
        m_dwPageSize = 0;
        m_dwPerPageCount = 0;
        m_dwPageCount = 0;

        DWORD dwSize = PageCount() * PageSize();

        // réservation de l'espace nécessaire au stockage
        m_pStore = VirtualAlloc(NULL, dwSize, MEM_RESERVE, PAGE_GUARD | PAGE_READWRITE);
        if (!m_pStore)
            return E_OUTOFMEMORY;

        // réservation de l'espace nécessaire à la gestion des cellules libres
        m_pFreeStore =
            (LPVOID*)VirtualAlloc(NULL, m_dwMaxObjects * sizeof(void*), MEM_RESERVE, PAGE_GUARD | PAGE_READWRITE);
        if (!m_pFreeStore)
        {
            VirtualFree(m_pStore, 0, MEM_RELEASE);
            return E_OUTOFMEMORY;
        }
        VirtualAlloc(m_pFreeStore, PageSize(), MEM_COMMIT, PAGE_READWRITE);

        m_dwCount = 0;
        m_pLastFreed = NULL;

        m_Initialized = true;
        LeaveCriticalSection(&m_cs);
        return S_OK;
    }

    // Nombre maxi d'éléments stockables
    DWORD MaxObject()
    {
        if (!m_Initialized)
            throw "Storage is not initialized!!!";
        return m_dwMaxObjects;
    }

    DWORD GetCount()
    {
        if (!m_Initialized)
            throw "Storage is not initialized!!!";
        return m_dwCount;
    }

    LPVOID GetNewCell()
    {

        if (!m_Initialized)
            throw "Storage is not initialized!!!";

        if (m_dwCount + 1 == MaxObject())
            return NULL;

        LPVOID retval = NULL;

        // Existe t'il des cellules libres à réutiliser
        if (m_pLastFreed)
        {
            // oui: les renvoyer afin d'assurer le remplissage du storage
            EnterCriticalSection(&m_cs);
            retval = *m_pLastFreed;
            *m_pLastFreed = NULL;
            if (m_pLastFreed == m_pFreeStore)
                m_pLastFreed = NULL;
            else
                m_pLastFreed--;
            LeaveCriticalSection(&m_cs);
        }
        else
        {
            // non: obtenir une nouvelle cellule à m_dwCount
            retval =
                (((LPBYTE)m_pStore) + ((m_dwCount / PerPageCount()) * PageSize())
                 + ((m_dwCount % PerPageCount()) * m_dwElementSize));
            if (retval == GetPageBaseAddress(retval))
                CommitPage(retval);
        }

        // incrémenter le compteur d'instances
        static_cast<void>(InterlockedIncrement(&m_dwCount));

        // incrémenter le compteur d'instances de la page
        static_cast<void>(InterlockedIncrement(GetPageUseCount(retval)));

        // on initialise la cellule
        InitializeCell(retval);

        return retval;
    }

    // libération d'une cellule
    void FreeCell(LPVOID cell)
    {

        if (!m_Initialized)
            throw "Storage is not initialized!!!";

        if (*((DWORD*)cell) == g_FreePattern)
        {
            // la cellule est déja libérée...
            throw L"Attempted to free a free cell\n";
        }

        // on marque la cellule libre
        *((DWORD*)cell) = g_FreePattern;

        // on décrémente le compteur d'instances
        static_cast<void>(InterlockedDecrement(&m_dwCount));

        // on décrémente le compte de page --> s'il passe à 0 on décommite la page
        DWORD* pUseCount = GetPageUseCount(cell);
        static_cast<void>(InterlockedDecrement(pUseCount));

        if (*pUseCount == 0)
        {

            EnterCriticalSection(&m_cs);

            if (m_pLastFreed > (m_pFreeStore))
            {
                LPVOID lpPageCandidateToFree = GetPageBaseAddress(cell);
                LPVOID* lpCurFree = m_pFreeStore;
                bool bFreeCellInPage = false;
                while (lpCurFree <= m_pLastFreed)
                {
                    if (GetPageBaseAddress(cell) == lpPageCandidateToFree)
                    {
                        bFreeCellInPage = true;
                    }
                    lpCurFree++;
                }
                if (!bFreeCellInPage)
                    VirtualFree(GetPageBaseAddress(cell), PageSize(), MEM_DECOMMIT);
            }
            else
            {
                // There is not other free cell, put this page in the free store
                if (m_pLastFreed)
                    m_pLastFreed++;
                else
                    m_pLastFreed = m_pFreeStore;
                *m_pLastFreed = GetPageBaseAddress(cell);
                m_pLastFreed++;
                *m_pLastFreed = (LPBYTE)GetPageBaseAddress(cell) + m_dwElementSize;
                m_pLastFreed++;
                *m_pLastFreed = (LPBYTE)GetPageBaseAddress(cell) + 2 * m_dwElementSize;
            }
            LeaveCriticalSection(&m_cs);
        }
        else
        {
            EnterCriticalSection(&m_cs);
            // --> l'espace de stockage contient une nouvelle cellule!
            if (m_pLastFreed)
                m_pLastFreed++;
            else
                m_pLastFreed = m_pFreeStore;

            // si les pages allouées du FreeStore est plein, on en redemande!
            if (m_pLastFreed != m_pFreeStore && GetPageBaseAddress(m_pLastFreed) == m_pLastFreed)
            {
                VirtualAlloc(m_pLastFreed, PageSize(), MEM_COMMIT, PAGE_READWRITE);
            }
            // on stocke la nouvelle cellule libérée dans le FreeStore
            *m_pLastFreed = cell;
            LeaveCriticalSection(&m_cs);
        }

        // le FreeStore est vide!!!
        if (!m_dwCount)
        {
            EnterCriticalSection(&m_cs);
            m_pLastFreed = NULL;
            LeaveCriticalSection(&m_cs);
        }
    }

    // dectruction du storage on libère globalement la mémoire
    ~ObjectStorage()
    {
        if (m_Initialized)
        {
            EnterCriticalSection(&m_cs);
            if (m_pStore)
            {
                if (!VirtualFree(m_pStore, 0, MEM_RELEASE))
                {
                    OutputDebugString(L"Failed to free Store memory!!\n");
                }
            }

            if (m_pFreeStore)
            {
                if (!VirtualFree(m_pFreeStore, 0, MEM_RELEASE))
                {
                    OutputDebugString(L"Failed to free FreeStore memory!!\n");
                }
            }
            m_Initialized = false;
            LeaveCriticalSection(&m_cs);
        }
    }
};

}  // namespace Orc

#pragma managed(pop)
