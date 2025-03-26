//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ByteQuantity.h"

using namespace Orc;

namespace {

static constexpr std::array<uint64_t, 4> kBinaryMultipliers =
    {1024, 1024 * 1024, 1024ULL * 1024 * 1024, 1024ULL * 1024 * 1024 * 1024};

static constexpr std::array<uint64_t, 4> kDecimalMultipliers =
    {1000, 1000 * 1000, 1000ULL * 1000 * 1000, 1000ULL * 1000 * 1000 * 1000};

static constexpr std::array<std::array<uint64_t, 4>, 2> kMultipliers = {kBinaryMultipliers, kDecimalMultipliers};

static_assert(
    std::underlying_type_t<ByteQuantityBase>(ByteQuantityBase::Base2) == 0,
    "Check enum values so they are ordered for 'kMultipliers'");

Result<uint64_t> GetMultiplier(char unit, ByteQuantityBase base)
{
    switch (unit)
    {
        case 'K':
            return kMultipliers[std::underlying_type_t<ByteQuantityBase>(base)][0];
        case 'M':
            return kMultipliers[std::underlying_type_t<ByteQuantityBase>(base)][1];
        case 'G':
            return kMultipliers[std::underlying_type_t<ByteQuantityBase>(base)][2];
        case 'T':
            return kMultipliers[std::underlying_type_t<ByteQuantityBase>(base)][3];
        default:
            return std::errc::invalid_argument;
    }
}

template <typename T>
Result<Traits::ByteQuantity<uint64_t>> ByteQuantityFromString(std::basic_string_view<T> view, ByteQuantityBase base)
{
    // Remove leading whitespace
    auto first_non_space = std::find_if(view.begin(), view.end(), [](T ch) { return !std::isspace(ch); });
    view = view.substr(std::distance(view.begin(), first_non_space));

    // Find the start of the unit
    auto unit_start =
        std::find_if(view.begin(), view.end(), [](T ch) { return !std::isdigit(ch); });

    // Extract the numeric and unit parts
    auto number_part = view.substr(0, std::distance(std::cbegin(view), unit_start));
    auto unit_part = view.substr(number_part.size(), std::distance(unit_start, std::cend(view)));

    // Trim leading whitespace from the unit part
    auto first_non_space_unit =
        std::find_if(unit_part.begin(), unit_part.end(), [](T ch) { return !std::isspace(ch); });
    unit_part = unit_part.substr(std::distance(unit_part.begin(), first_non_space_unit));

    // Trim trailing whitespace from the unit part
    auto last_non_space_unit =
        std::find_if(unit_part.rbegin(), unit_part.rend(), [](T ch) { return !std::isspace(ch); }).base();
    unit_part = unit_part.substr(0, std::distance(unit_part.begin(), last_non_space_unit));

    // Convert the numeric part to uint64_t
    uint64_t value = 0;
    try
    {
        value = std::stoull(std::basic_string<T>(number_part.begin(), number_part.end()));
    }
    catch (const std::invalid_argument&)
    {
        return std::errc::invalid_argument;
    }
    catch (const std::out_of_range&)
    {
        return std::errc::result_out_of_range;
    }

    if (unit_part.size() > 2 || unit_part.size() > 1 && unit_part[1] != 'B')
    {
        return std::errc::invalid_argument;
    }

    uint64_t multiplier = 1;
    if (!unit_part.empty() && unit_part[0] != 'B')
    {
        auto rv = GetMultiplier(unit_part[0], base);
        if (!rv)
        {
            return rv.error();
        }

        multiplier = *rv;
    }

    if (value > std::numeric_limits<uint64_t>::max() / multiplier)
    {
        return std::errc::result_out_of_range;
    }

    return Orc::ByteQuantity<uint64_t>(value * multiplier);
}

}  // namespace

namespace Orc {
namespace Text {

Result<Orc::ByteQuantity<uint64_t>> ByteQuantityFromString(std::string_view view, ByteQuantityBase base)
{
    return ::ByteQuantityFromString(view, base);
}

Result<Orc::ByteQuantity<uint64_t>> ByteQuantityFromString(std::wstring_view view, ByteQuantityBase base)
{
    return ::ByteQuantityFromString(view, base);
}

}  // namespace Text
}  // namespace Orc
