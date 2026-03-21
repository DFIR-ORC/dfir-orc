//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2024 ANSSI. All Rights Reserved.
//
// Author(s): Copilot (refactor)
//

#include "WolfPriority.h"

namespace Orc {

std::wstring ToString(WolfPriority value)
{
    switch (value)
    {
        case WolfPriority::Low:
            return L"Low";
        case WolfPriority::Normal:
            return L"Normal";
        case WolfPriority::High:
            return L"High";
        case WolfPriority::Idle:
            return L"Idle";
        default:
            return L"<Unknown>";
    }
}

WolfPriority WolfPriorityFromPriorityClass(uint32_t priorityClass)
{
    switch (priorityClass)
    {
        case IDLE_PRIORITY_CLASS:
            return WolfPriority::Idle;
        case BELOW_NORMAL_PRIORITY_CLASS:
            return WolfPriority::Low;
        case NORMAL_PRIORITY_CLASS:
            return WolfPriority::Normal;
        case ABOVE_NORMAL_PRIORITY_CLASS:
        case HIGH_PRIORITY_CLASS:
        case REALTIME_PRIORITY_CLASS:
            return WolfPriority::High;
        default:
            return WolfPriority::Normal;
    }
}

typedef enum _WINAPI_IO_PRIORITY_HINT
{
    IoPriorityVeryLow = 0,  // Defragging, content indexing and other background I/Os.
    IoPriorityLow,  // Prefetching for applications.
    IoPriorityNormal,  // Normal I/Os.
    IoPriorityHigh,  // Used by filesystems for checkpoint I/O.
    IoPriorityCritical,  // Used by memory manager. Not available for applications.
    MaxIoPriorityTypes
} WINAPI_IO_PRIORITY_HINT;

uint32_t ToIoPriorityHint(WolfPriority priority)
{
    switch (priority)
    {
        case WolfPriority::Idle:
            return IoPriorityLow;
        case WolfPriority::Low:
            return IoPriorityLow;
        case WolfPriority::Normal:
            return IoPriorityNormal;
        case WolfPriority::High:
            return IoPriorityHigh;
        default:
            return IoPriorityNormal;
    }
}

typedef enum _WINAPI_MEMORY_PRIORITY
{
    WINAPI_MEMORY_PRIORITY_VERY_LOW = 1,  // The process is unlikely to be trimmed.
    WINAPI_MEMORY_PRIORITY_LOW = 2,  // The process is somewhat likely to be trimmed.
    WINAPI_MEMORY_PRIORITY_MEDIUM = 3,  // The process is likely to be trimmed.
    WINAPI_MEMORY_PRIORITY_BELOW_NORMAL = 4,  // The process is more likely to be trimmed than normal.
    WINAPI_MEMORY_PRIORITY_NORMAL = 5,  // The default priority for processes. The process is no more or less likely to be
                                 // trimmed than other normal priority processes.
} WINAPI_MEMORY_PRIORITY;

uint32_t ToMemoryPriority(WolfPriority priority)
{
    switch (priority)
    {
        case WolfPriority::Idle:
            return WINAPI_MEMORY_PRIORITY_VERY_LOW;
        case WolfPriority::Low:
            return WINAPI_MEMORY_PRIORITY_LOW;
        case WolfPriority::Normal:
        case WolfPriority::High:
            return WINAPI_MEMORY_PRIORITY_NORMAL;
        default:
            return WINAPI_MEMORY_PRIORITY_NORMAL;
    }
}

}  // namespace Orc
