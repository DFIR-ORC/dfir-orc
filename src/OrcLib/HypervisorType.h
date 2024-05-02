#pragma once

#include "Utils/Result.h"

namespace Orc {

enum class HypervisorType
{
    Undefined = 0,
    None,
    HyperV,
    VMware,
    VirtualBox,
    QEMU,
    Xen,
    EnumCount
};

std::string_view ToStringView(HypervisorType type);
std::wstring ToStringW(HypervisorType type);

Result<HypervisorType> ToHypervisorType(std::string_view record_type);

}  // namespace Orc
