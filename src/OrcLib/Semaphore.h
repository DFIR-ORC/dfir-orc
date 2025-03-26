//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
// A Semaphore type that uses cooperative blocking semantics.

#pragma once

#include <Windows.h>

#include <concrt.h>
#include <concurrent_queue.h>

#pragma managed(push, off)

#undef Yield

namespace Orc {

class Semaphore
{
public:
    // An exception-safe RAII wrapper for the Semaphore class.

    class ScopedLock
    {
    public:
        // Acquires access to the Semaphore.

        ScopedLock(Semaphore& s)
            : _s(s)
        {
            _s.Acquire();
        }
        // Releases access to the Semaphore.

        ~ScopedLock() { _s.Release(); }

    private:
        Semaphore& _s;
    };

    explicit Semaphore(LONG capacity)
        : _semaphore_count(capacity)
    {
    }

    // Acquires access to the Semaphore.

    void Acquire()
    {
        // The capacity of the Semaphore is exceeded when the Semaphore count

        // falls below zero. When this happens, add the current context to the

        // back of the wait queue and block the current context.

        if (InterlockedDecrement(&_semaphore_count) < 0)
        {
            _waiting_contexts.push(Concurrency::Context::CurrentContext());
            Concurrency::Context::Block();
        }
    }

    // Tries to access to the Semaphore.
    // if The capacity of the Semaphore is exceeded don't block and return false
    bool TryAcquire()
    {
        // The capacity of the Semaphore is exceeded when the Semaphore count

        // falls below zero. When this happens, add the current context to the

        // back of the wait queue and block the current context.

        if (InterlockedDecrement(&_semaphore_count) < 0)
        {
            // would block, reincrement and return false
            static_cast<void>(InterlockedIncrement(&_semaphore_count));
            return false;
        }
        // acquired the semaphore, return true;
        return true;
    }

    // Releases access to the Semaphore.

    void Release()
    {
        // If the Semaphore count is negative, unblock the first waiting context.

        if (InterlockedIncrement(&_semaphore_count) <= 0)
        {
            // A call to acquire might have decremented the counter, but has not

            // yet finished adding the context to the queue.

            // Create a spin loop that waits for the context to become available.

            Concurrency::Context* waiting = NULL;
            if (!_waiting_contexts.try_pop(waiting))
            {
                Concurrency::Context::Yield();
            }

            // Unblock the context.
            waiting->Unblock();
        }
    }

private:
    // The Semaphore count.

    LONG _semaphore_count;

    // A concurrency-safe queue of contexts that must wait to

    // acquire the Semaphore.

    Concurrency::concurrent_queue<Concurrency::Context*> _waiting_contexts;
};
}  // namespace Orc

constexpr auto Yield() {};

#pragma managed(pop)
