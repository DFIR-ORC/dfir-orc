//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ApplicationInformation.h"
#include "Text/Fmt/GUID.h"

namespace {

template <typename LengthT, typename CharT = char>
static std::basic_string_view<CharT> DecodeLengthValue(Orc::BufferView buffer, std::error_code& ec)
{
    auto input = buffer;

    if (input.size() < sizeof(LengthT))
    {
        ec = std::make_error_code(std::errc::value_too_large);
        return {};
    }

    const LengthT* length = reinterpret_cast<const LengthT*>(input.data());
    input = input.subspan(sizeof(LengthT));

    if (input.size() < *length)
    {
        ec = std::make_error_code(std::errc::value_too_large);
        return {};
    }

    auto output =
        std::basic_string_view<CharT>(reinterpret_cast<const CharT*>(input.data()), (*length) / sizeof(CharT));
    buffer = input.subspan(*length);
    return output;
}

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

const GUID ApplicationInformation::VssLocalInfo::kMicrosoftGuid =
    {0xe5de7d45, 0x49f2, 0x40a4, 0x81, 0x7c, 0x7d, 0xc8, 0x2b, 0x72, 0x58, 0x7f};

const GUID ApplicationInformation::VssLocalInfo::kMicrosoftHiddenGuid =
    {0xf12142b4, 0x9a4b, 0x49af, 0xa8, 0x51, 0x70, 0x0c, 0x42, 0xfd, 0xc2, 0xbe};

void ApplicationInformation::Parse(BufferView buffer, ApplicationInformation& info, std::error_code& ec)
{
    if (buffer.size() < sizeof(ApplicationInformation::kBlockSize))
    {
        Log::Debug("Failed to parse VSS ApplicationInfo: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const ApplicationInformation::Layout*>(buffer.data());
    Node::Parse(layout.node, info.m_node, ec);
    if (ec)
    {
        Log::Debug("Failed to parse VSS ApplicationInfo: failed to parse node");
        return;
    }

    if (info.m_node.Type() != NodeType::kApplicationInfo)
    {
        Log::Debug("Failed to parse VSS ApplicationInfo: unexpected node type");
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    info.m_applicationInfoSize = *reinterpret_cast<const uint64_t*>(layout.applicationInfoSize);

    std::string_view padding(reinterpret_cast<const char*>(layout.padding), sizeof(layout.padding));
    if (!std::all_of(std::cbegin(padding), std::cend(padding), [](char c) { return c == 0; }))
    {
        Log::Debug("Unexpected VSS ApplicationInfo non-zero padding");
    }

    VssLocalInfo vssLocalInfo;
    VssLocalInfo::Parse(buffer.subspan(sizeof(ApplicationInformation::Layout)), vssLocalInfo, ec);
    if (ec)
    {
        Log::Debug("Failed to parse VSS ApplicationInfo custom format [{}]", ec);

        if (ec == std::errc::not_supported)
        {
            // 'VssLocalInfo::kMicrosoftGuid' or 'VssLocalInfo::kMicrosoftHiddenGuid' are expected. Give a try anyway.
            ec.clear();
        }
    }
    else
    {
        // Consitency check of the parsed VssLocalInfo size against expected size
        const auto applicationInfoSize = sizeof(ApplicationInformation::VssLocalInfo::Layout) + sizeof(uint16_t)
            + vssLocalInfo.MachineString().size() * sizeof(wchar_t) + sizeof(uint16_t)
            + vssLocalInfo.ServiceString().size() * sizeof(wchar_t);
        if (applicationInfoSize != info.m_applicationInfoSize)
        {
            Log::Debug(
                "Failed VSS ApplicationInfo consistency check: invalid parsed size (stored: {}, computed: {})",
                info.m_applicationInfoSize,
                applicationInfoSize);
        }

        info.m_value = std::move(vssLocalInfo);
    }
}

void ApplicationInformation::VssLocalInfo::Parse(
    BufferView buffer,
    ApplicationInformation::VssLocalInfo& info,
    std::error_code& ec)
{
    if (buffer.size() < sizeof(ApplicationInformation::VssLocalInfo::Layout))
    {
        Log::Debug("Failed to parse VSS ApplicationInfo::VssLocalInfo: invalid size");
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    auto& layout = *reinterpret_cast<const ApplicationInformation::VssLocalInfo::Layout*>(buffer.data());

    info.m_guid = *reinterpret_cast<const GUID*>(&layout.guid);

    if (info.m_guid != VssLocalInfo::kMicrosoftGuid && info.m_guid != VssLocalInfo::kMicrosoftHiddenGuid)
    {
        Log::Warn("Unexpected VSS ApplicationInfo::VssLocalInfo: unexpected guid '{}'", info.m_guid);
    }

    info.m_shadowCopyGuid = *reinterpret_cast<const GUID*>(&layout.shadowCopyGuid);
    info.m_shadowCopySetGuid = *reinterpret_cast<const GUID*>(&layout.shadowCopySetGuid);

    const auto context = *reinterpret_cast<const uint32_t*>(&layout.snapshotContext);
    info.m_snapshotContext = ToSnapshotContext(context, ec);
    if (ec)
    {
        Log::Debug(
            "Failed VSS ApplicationInfo::VssLocalInfo consistency check: unknown context flag(s) in {:#x}", context);
        ec.clear();
    }

    info.m_snapshotsCount = *reinterpret_cast<const uint32_t*>(&layout.snapshotsCount);

    const auto attributes = *reinterpret_cast<const uint32_t*>(&layout.attributeFlags);
    info.m_volumeSnapshotAttributes = ToVolumeSnapshotAttributes(attributes, ec);
    if (ec)
    {
        Log::Debug(
            "Failed VSS ApplicationInfo::VssLocalInfo consistency check: unknown volume snapshot flag(s) in {:#x}",
            attributes);
        ec.clear();
    }

    auto dynamicLayout = buffer.subspan(sizeof(Layout));
    info.m_machineString = DecodeLengthValue<uint16_t, wchar_t>(dynamicLayout, ec);
    if (ec)
    {
        Log::Debug("Failed VSS ApplicationInfo::VssLocalInfo machine string parsing [{}]", ec);
        ec.clear();
    }

    info.m_serviceString = DecodeLengthValue<uint16_t, wchar_t>(dynamicLayout, ec);
    if (ec)
    {
        Log::Debug("Failed VSS ApplicationInfo::VssLocalInfo service string parsing [{}]", ec);
        ec.clear();
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
