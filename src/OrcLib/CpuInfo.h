//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "CpuId.h"

namespace Orc {

class CpuInfo final
{
public:
    CpuInfo();

    const CpuId& CpuId() const;

    const std::string& Manufacturer() const;
    const std::string& Name() const;

    Result<uint32_t> PhysicalCores() const;
    Result<uint32_t> LogicalCores() const;
    Result<bool> IsHyperThreadingEnabled() const;

private:
    Orc::CpuId m_cpuId;
    std::string m_manufacturer;
    std::string m_name;
};

}  // namespace Orc
