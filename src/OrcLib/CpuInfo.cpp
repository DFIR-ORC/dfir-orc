//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "CpuInfo.h"

// BEWARE: do not rely on CPUID to get the number of logical and physical processor.
//         See: https://stackoverflow.com/a/24824554

#include "Utils/Result.h"

namespace {

struct Vendor
{
    std::string_view ManufacturerId;
    std::string_view FriendlyName;
};

constexpr std::array kVendors = {
    Vendor {Orc::CpuId::kIntelManufacturerId, {"Intel"}},
    Vendor {Orc::CpuId::kAmdManufacturerId, {"AMD"}},
    Vendor {{"Microsoft Hv"}, {"Microsoft Hyper-V"}},
    Vendor {{"MicrosoftXTA"}, {"Microsoft XTA"}},
    Vendor {{"VMwareVMware"}, {"VMware"}},
    Vendor {{"XenVMMXenVMM"}, {"Xen HVM"}},
    Vendor {{"KVMKVMKVM"}, {"KVM"}},
    Vendor {{"TCGTCGTCGTCG"}, {"QEMU"}},
    Vendor {{" lrpepyh  vr"}, {"Parallels"}},
    Vendor {{"VirtualApple"}, {"Virtual Apple"}},
};

std::string_view ToManufacturer(std::string_view manufacturerId)
{
    auto it = std::find_if(std::cbegin(kVendors), std::cend(kVendors), [&](auto& vendor) {
        return vendor.ManufacturerId == manufacturerId;
    });

    if (it == std::cend(kVendors))
    {
        return manufacturerId;
    }

    return it->FriendlyName;
}

std::string_view ToName(std::string_view brand)
{
    uint32_t padding = 0;

    for (int i = brand.size() - 1; i >= 0; --i)
    {
        if (std::isspace(brand[i]) || brand[i] == 0)
        {
            ++padding;
        }
        else
        {
            break;
        }
    }

    return brand.substr(0, brand.size() - padding);
}

Orc::Result<std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION>> GetLogicalProcessorInformationApi()
{
    using FnGetLogicalProcessorInformation =
        BOOL(__stdcall*)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer, PDWORD ReturnedLength);

    static FnGetLogicalProcessorInformation fn = nullptr;
    if (fn == nullptr)
    {
        fn = reinterpret_cast<FnGetLogicalProcessorInformation>(
            ::GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetLogicalProcessorInformation"));

        if (!fn)
        {
            return Orc::LastWin32Error();
        }
    }

    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info;
    info.resize(1);
    DWORD length = info.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    for (;;)
    {
        BOOL ret = fn(reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(info.data()), &length);
        if (!ret)
        {
            auto error = GetLastError();
            if (error != ERROR_INSUFFICIENT_BUFFER)
            {
                return Orc::Win32Error(error);
            }

            info.resize(1 + (length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION)));
            length = info.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            continue;
        }

        info.resize(length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        break;
    }

    return info;
}

Orc::Result<std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>>
GetLogicalProcessorInformationExApi(LOGICAL_PROCESSOR_RELATIONSHIP relationship)
{
    using FnGetLogicalProcessorInformationEx = BOOL(__stdcall*)(
        LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer,
        PDWORD ReturnedLength);

    static FnGetLogicalProcessorInformationEx fn = nullptr;
    if (fn == nullptr)
    {
        fn = reinterpret_cast<FnGetLogicalProcessorInformationEx>(
            ::GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetLogicalProcessorInformationEx"));

        if (!fn)
        {
            return Orc::LastWin32Error();
        }
    }

    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX> info;
    info.resize(1);
    DWORD length = info.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    for (;;)
    {
        BOOL ret = fn(relationship, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(info.data()), &length);
        if (!ret)
        {
            auto error = GetLastError();
            if (error != ERROR_INSUFFICIENT_BUFFER)
            {
                return Orc::Win32Error(error);
            }

            info.resize(1 + (length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)));
            length = info.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
            continue;
        }

        info.resize(length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        break;
    }

    return info;
}

Orc::Result<uint32_t> GetActiveProcessorCountApi(WORD GroupNumber)
{
    using FnGetActiveProcessorCount = BOOL(__stdcall*)(DWORD GroupNumber);

    static FnGetActiveProcessorCount fn = nullptr;
    if (fn == nullptr)
    {
        fn = reinterpret_cast<FnGetActiveProcessorCount>(
            ::GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetActiveProcessorCount"));

        if (!fn)
        {
            return Orc::LastWin32Error();
        }
    }

    return fn(GroupNumber);
}

DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR) * 8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
    DWORD i;

    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest) ? 1 : 0);
        bitTest /= 2;
    }

    return bitSetCount;
}

}  // namespace

namespace Orc {

CpuInfo::CpuInfo()
    : m_cpuId()
    , m_manufacturer(::ToManufacturer(m_cpuId.ManufacturerId()))
    , m_name(::ToName(m_cpuId.Brand()))
{
}

const CpuId& CpuInfo::CpuId() const
{
    return m_cpuId;
}

const std::string& CpuInfo::Manufacturer() const
{
    return m_manufacturer;
}

const std::string& CpuInfo::Name() const
{
    return m_name;
}

Result<uint32_t> CpuInfo::PhysicalCores() const
{
    //
    // How to correctly get the number of processors ?
    // See: https://devblogs.microsoft.com/oldnewthing/20200824-00/?p=104116
    //

    if (auto processorCount = ::GetActiveProcessorCountApi(ALL_PROCESSOR_GROUPS))
    {
        return processorCount.value();
    }
    else
    {
        Log::Debug("Failed GetActiveProcessorCount [{}]", processorCount.error());
    }

    // See: https://devblogs.microsoft.com/oldnewthing/20200824-00/?p=104116
    if (auto logicalProcessorInfo =
            ::GetLogicalProcessorInformationExApi(LOGICAL_PROCESSOR_RELATIONSHIP::RelationGroup))
    {
        uint32_t processorCount = 0;
        for (const auto& info : *logicalProcessorInfo)
        {
            std::basic_string_view<PROCESSOR_GROUP_INFO> groups(info.Group.GroupInfo, info.Group.ActiveGroupCount);
            for (auto& group : groups)
            {
                processorCount += info.Group.GroupInfo->ActiveProcessorCount;
            }
        }

        return processorCount;
    }
    else
    {
        Log::Debug("Failed GetLogicalProcessInformationEx [{}]", logicalProcessorInfo.error());
    }

    // See: https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformation
    if (auto logicalProcessorInfo = ::GetLogicalProcessorInformationApi())
    {
        uint32_t processorCount = 0;
        for (const auto& info : *logicalProcessorInfo)
        {
            if (info.Relationship == LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore)
            {
                processorCount++;
            }
        }

        return processorCount;
    }
    else
    {
        Log::Debug("Failed GetLogicalProcessInformation [{}]", logicalProcessorInfo.error());
    }

    // As a last resort
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}

Result<uint32_t> CpuInfo::LogicalCores() const
{
    // See: https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformation
    if (auto logicalProcessorInfo = ::GetLogicalProcessorInformationApi())
    {
        uint32_t logicalProcessorCount = 0;
        for (const auto& info : *logicalProcessorInfo)
        {
            if (info.Relationship == LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore)
            {
                logicalProcessorCount += CountSetBits(info.ProcessorMask);
            }
        }

        return logicalProcessorCount;
    }
    else
    {
        Log::Debug("Failed GetLogicalProcessInformation [{}]", logicalProcessorInfo.error());
    }

    return std::thread::hardware_concurrency();
}

Result<bool> CpuInfo::IsHyperThreadingEnabled() const
{
    if (!m_cpuId.HasHyperThreading())
    {
        return false;
    }

    auto logicalCores = LogicalCores();
    if (!logicalCores)
    {
        return logicalCores.error();
    }

    auto physicalCores = PhysicalCores();
    if (!physicalCores)
    {
        return physicalCores.error();
    }

    return physicalCores.value() < logicalCores.value();
}

}  // namespace Orc
