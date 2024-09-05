//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <windows.h>

#pragma managed(push, off)

namespace Orc {

class ScopedLock;
class CriticalSection
{
public:
    CriticalSection() { InitializeCriticalSection(&m_CS); }
    ~CriticalSection() { DeleteCriticalSection(&m_CS); }

private:
    friend class ScopedLock;
    void lock() { EnterCriticalSection(&m_CS); }
    void unlock() { LeaveCriticalSection(&m_CS); }

private:
    CRITICAL_SECTION m_CS;
};

class ScopedLock
{
public:
    ScopedLock(CriticalSection& cs)
        : m_cs(cs)
    {
        m_cs.lock();
    }
    ~ScopedLock(void) { m_cs.unlock(); }

private:
    CriticalSection& m_cs;
};

}  // namespace Orc

#pragma managed(pop)
