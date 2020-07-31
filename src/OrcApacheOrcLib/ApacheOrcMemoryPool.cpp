//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ApacheOrcMemoryPool.h"

#include "OrcException.h"

#include <safeint.h>

using namespace Orc::TableOutput::ApacheOrc;

using namespace msl::utilities;

Orc::TableOutput::ApacheOrc::MemoryPool::MemoryPool(size_t initialSize)
    : m_initialSize(initialSize)
{
}

char* Orc::TableOutput::ApacheOrc::MemoryPool::malloc(uint64_t size)
{
    CheckHeap();

    auto lpVoid = HeapAlloc(m_heap, HEAP_NO_SERIALIZE, SafeInt<size_t>(size));
    if (lpVoid == nullptr)
        throw Orc::Exception(Fatal, HRESULT_FROM_WIN32(GetLastError()), L"Failed to allocate heap memory %z", size);

    return (char*)lpVoid;
}

void Orc::TableOutput::ApacheOrc::MemoryPool::free(char* p)
{
    CheckHeap();

    if (!HeapFree(m_heap, HEAP_NO_SERIALIZE, p))
    {
        throw Orc::Exception(Fatal, HRESULT_FROM_WIN32(GetLastError()), L"Failed to free heap memory");
    }
}

Orc::TableOutput::ApacheOrc::MemoryPool::~MemoryPool()
{
    if (m_heap != NULL)
    {
        HeapDestroy(m_heap);
        m_heap = NULL;
    }
}

void Orc::TableOutput::ApacheOrc::MemoryPool::InitializeHeap()
{
    if (m_heap != NULL)
    {
        if (!HeapDestroy(m_heap))
            throw Orc::Exception(Fatal, HRESULT_FROM_WIN32(GetLastError()), L"Failed to destroy existing heap");
        m_heap = NULL;
    }

    m_heap = HeapCreate(HEAP_NO_SERIALIZE, m_initialSize, 0);
    if (m_heap == NULL)
        throw Orc::Exception(Fatal, HRESULT_FROM_WIN32(GetLastError()), L"Failed to initialize heap");
}
