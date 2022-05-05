//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <memory>
#include <variant>

#include <windows.h>

#include "Filesystem/Ntfs/ShadowCopy/Node.h"
#include "Filesystem/Ntfs/ShadowCopy/SnapshotContext.h"
#include "Filesystem/Ntfs/ShadowCopy/VolumeSnapshotAttributes.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class ApplicationInformation final
{
public:
    const static uint16_t kBlockSize = 16384;

    class VssLocalInfo
    {
    public:
        static const GUID kMicrosoftGuid;
        static const GUID kMicrosoftHiddenGuid;

        static void Parse(BufferView buffer, VssLocalInfo& vssLocalInfo, std::error_code& ec);

#pragma pack(push, 1)
        struct Layout
        {
            uint8_t guid[16];
            uint8_t shadowCopyGuid[16];
            uint8_t shadowCopySetGuid[16];
            uint8_t snapshotContext[4];
            uint8_t snapshotsCount[4];
            uint8_t attributeFlags[4];
            uint8_t unknown2[4];

            // Dynamic size up to Layout's maximum size of kBlockSize
            // uint8_t machineStringSize[2];
            // uint8_t machineString[machineStringSize];  // Originating Machine
            // uint8_t serviceStringSize[2];
            // uint8_t serviceString[serviceStringSize];  // Service Machine
            // uint8_t padding[?];  // zero bytes padding to applicationInfoSize or kBlockSize
        };
#pragma pack(pop)

        VssLocalInfo()
            : m_guid()
            , m_shadowCopyGuid()
            , m_shadowCopySetGuid()
            , m_snapshotContext(Ntfs::ShadowCopy::SnapshotContext {0})
            , m_snapshotsCount(0)
            , m_volumeSnapshotAttributes(Ntfs::ShadowCopy::VolumeSnapshotAttributes {0})
            , m_machineString()
            , m_serviceString()
        {
        }

        const GUID& Guid() const { return m_guid; }
        const GUID& ShadowCopyId() const { return m_shadowCopyGuid; }
        const GUID& ShadowCopySetId() const { return m_shadowCopySetGuid; }
        SnapshotContext SnapshotContext() const { return m_snapshotContext; }
        uint32_t SnapshotsCount() const { return m_snapshotsCount; }
        VolumeSnapshotAttributes VolumeSnapshotAttributes() const { return m_volumeSnapshotAttributes; }
        const std::wstring& MachineString() const { return m_machineString; }
        const std::wstring& ServiceString() const { return m_serviceString; }

    private:
        GUID m_guid;
        GUID m_shadowCopyGuid;
        GUID m_shadowCopySetGuid;
        Ntfs::ShadowCopy::SnapshotContext m_snapshotContext;
        uint32_t m_snapshotsCount;
        Ntfs::ShadowCopy::VolumeSnapshotAttributes m_volumeSnapshotAttributes;
        std::wstring m_machineString;
        std::wstring m_serviceString;
    };

    static void Parse(BufferView buffer, ApplicationInformation& info, std::error_code& ec);

    //
    // Following is data sent from userland with IOCTL_VOLSNAP_SET_APPLICATION_INFO
    //
#pragma pack(push, 1)
    struct Layout
    {
        Node::Layout node;
        uint8_t applicationInfoSize[8];
        uint8_t padding[72];  // zero bytes padding to 128
    };
#pragma pack(pop)

    static_assert(sizeof(Layout) == 128);

    const Node& Node() { return m_node; }
    const uint64_t Size() const { return m_applicationInfoSize; }

    template <typename T>
    T* Get()
    {
        return std::get_if<T>(&m_value);
    }

    template <typename T>
    const T* Get() const
    {
        return std::get_if<T>(&m_value);
    }

private:
    Ntfs::ShadowCopy::Node m_node;
    uint64_t m_applicationInfoSize;
    std::variant<std::monostate, VssLocalInfo> m_value;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
