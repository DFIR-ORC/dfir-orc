//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#pragma warning(disable : 4521)
#include "orc/MemoryPool.hh"
#pragma warning(default : 4521)

#include "OrcException.h"

namespace Orc::TableOutput::ApacheOrc {

class MemoryPool : public orc::MemoryPool
{

public:
    MemoryPool(size_t initialSize = 0L);

    virtual char* malloc(uint64_t size) override final;
    virtual void free(char* p) override final;

    ~MemoryPool();

private:
    void CheckHeap()
    {
        if (m_heap == NULL)
            InitializeHeap();
        if (m_heap == NULL)
            throw Orc::Exception(L"Failed to initialize heap for orc memory pool");
    }
    void InitializeHeap();

    HANDLE m_heap = NULL;
    size_t m_initialSize = 0;
};

}  // namespace Orc::TableOutput::OptRowColumn
