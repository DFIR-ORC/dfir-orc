//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <concrt.h>
#include <concurrent_queue.h>

#undef Yield

#pragma managed(push, off)

namespace Orc {

// A semaphore type that uses cooperative blocking semantics.
class ImportBytesSemaphore
{
public:
    explicit ImportBytesSemaphore() {}

    void SetCapacity(long long Capacity) { _count = Capacity; }

    // Acquires access to the semaphore.
    void acquire(long long addedBytes)
    {
        // The capacity of the semaphore is exceeded when the semaphore count
        // falls below zero. When this happens, add the current context to the
        // back of the wait queue and block the current context.
        if ((_count -= addedBytes) < 0)
        {
            _waiting_contexts.push(Concurrency::Context::CurrentContext());
            Concurrency::Context::Block();
        }
    }

    // Releases access to the semaphore.
    void release(long long processedBytes)
    {
        // If the semaphore count is negative, unblock the first waiting context.
        if ((_count += processedBytes) <= 0)
        {
            // A call to acquire might have decremented the counter, but has not
            // yet finished adding the context to the queue.
            // Create a spin loop that waits for the context to become available.
            Concurrency::Context* waiting = NULL;
            while (!_waiting_contexts.try_pop(waiting))
            {
                Concurrency::Context::Yield();
            }

            // Unblock the context.
            waiting->Unblock();
        }
    }

private:
    // The semaphore count.
    std::atomic<long long> _count;

    // A concurrency-safe queue of contexts that must wait to
    // acquire the semaphore.
    Concurrency::concurrent_queue<Concurrency::Context*> _waiting_contexts;
};
}  // namespace Orc
#pragma managed(pop)
