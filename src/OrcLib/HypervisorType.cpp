#include "HypervisorType.h"

using namespace Orc;
using namespace std::literals::string_view_literals;

namespace Orc {

constexpr auto enum_count = static_cast<std::underlying_type_t<HypervisorType>>(HypervisorType::EnumCount);
constexpr std::array<std::tuple<HypervisorType, std::string_view>, enum_count + 1> kHypervisorTypeEntries = {
    std::make_tuple(HypervisorType::Undefined, "<undefined>"sv),
    std::make_tuple(HypervisorType::None, "none"sv),
    std::make_tuple(HypervisorType::HyperV, "hyper-v"sv),
    std::make_tuple(HypervisorType::VMware, "vwware"sv),
    std::make_tuple(HypervisorType::VirtualBox, "virtualbox"sv),
    std::make_tuple(HypervisorType::QEMU, "qemu"sv),
    std::make_tuple(HypervisorType::Xen, "xen"sv),
    std::make_tuple(HypervisorType::EnumCount, "<unknown>"sv)};

Result<HypervisorType> ToHypervisorType(std::string_view magic)
{
    using underlying_type = std::underlying_type_t<HypervisorType>;

    for (auto i = 1; i < kHypervisorTypeEntries.size() - 1; i++)
    {
        if (magic == std::get<1>(kHypervisorTypeEntries[i]))
        {
            return static_cast<HypervisorType>(std::get<0>(kHypervisorTypeEntries[i]));
        }
    }

    return std::make_error_code(std::errc::invalid_argument);
}

std::string_view ToStringView(HypervisorType type)
{
    using underlying_type = std::underlying_type_t<HypervisorType>;
    constexpr auto enum_count = static_cast<underlying_type>(HypervisorType::EnumCount);

    auto id = static_cast<underlying_type>(type);
    if (id >= enum_count)
    {
        id = static_cast<underlying_type>(HypervisorType::EnumCount);
    }

    return std::get<1>(kHypervisorTypeEntries[id]);
}

std::wstring ToStringW(HypervisorType type)
{
    auto view = ToStringView(type);
    return {std::cbegin(view), std::cend(view)};
}

}  // namespace Orc
