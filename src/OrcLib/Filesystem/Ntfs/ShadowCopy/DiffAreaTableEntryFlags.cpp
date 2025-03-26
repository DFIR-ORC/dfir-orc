//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "DiffAreaTableEntryFlags.h"

#include <tuple>

#include "Log/Log.h"
#include "Utils/String.h"

namespace {

constexpr std::string_view kUnknown = "<unknown>";
constexpr std::string_view kNone = "<none>";

constexpr std::string_view kForwarder = "forwarder";
constexpr std::string_view kOverlay = "overlay";
constexpr std::string_view kUnused = "unused";
constexpr std::string_view kUnknown8 = "0x8";
constexpr std::string_view kUnknown10 = "0x10";
constexpr std::string_view kUnknown20 = "0x20";
constexpr std::string_view kUnknown40 = "0x40";
constexpr std::string_view kUnknown80 = "0x80";

constexpr std::array kStoreEntryFlags = {
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kForwarder, kForwarder},
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kOverlay, kOverlay},
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kUnused, kUnused},
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kUnknown8, kUnknown8},
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kUnknown10, kUnknown10},
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kUnknown20, kUnknown20},
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kUnknown40, kUnknown40},
    std::tuple {Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags::kUnknown80, kUnknown80},

};

bool HasInvalidFlag(uint32_t flags)
{
    using namespace Orc::Ntfs::ShadowCopy;
    using namespace Orc;

    auto remainingFlags = flags;
    for (const auto& [flag, description] : kStoreEntryFlags)
    {
        if (HasFlag(static_cast<DiffAreaTableEntryFlags>(flags), flag))
        {
            remainingFlags -= static_cast<std::underlying_type_t<DiffAreaTableEntryFlags>>(flag);
        }
    }

    if (remainingFlags)
    {
        return true;
    }

    return false;
}

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

std::string ToString(DiffAreaTableEntryFlags flags)
{
    if (flags == static_cast<DiffAreaTableEntryFlags>(0))
    {
        return std::string(kNone);
    }

    if (::HasInvalidFlag(std::underlying_type_t<DiffAreaTableEntryFlags>(flags)))
    {
        Log::Debug(
            "Failed to convert some flags to string (value: {})",
            std::underlying_type_t<DiffAreaTableEntryFlags>(flags));

        return fmt::format("{:#x}", std::underlying_type_t<DiffAreaTableEntryFlags>(flags));
    }

    std::vector<std::string_view> strings;

    for (const auto& [flag, description] : kStoreEntryFlags)
    {
        if (HasFlag(flags, flag))
        {
            strings.push_back(description);
        }
    }

    std::string output;
    Join(std::cbegin(strings), std::cend(strings), output, '|');
    return output;
}

DiffAreaTableEntryFlags ToDiffAreaEntryFlags(const uint32_t attributes, std::error_code& ec)
{
    if (::HasInvalidFlag(std::underlying_type_t<DiffAreaTableEntryFlags>(attributes)))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
    }

    return static_cast<DiffAreaTableEntryFlags>(attributes);
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
