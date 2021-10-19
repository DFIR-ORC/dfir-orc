//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Log/Log.h"
#include "Filesystem/Ntfs/Compression/WofAlgorithm.h"
#include "Text/Fmt/std_error_code.h"
#include "Utils/BufferView.h"
#include "Utils/Result.h"

namespace Orc {
namespace Ntfs {

enum class WofProviderType : uint32_t
{
    kWim = 1,
    kFile = 2
};

class WofReparsePoint
{
public:
#pragma pack(push, 1)
    struct Layout
    {
        uint32_t version;
        uint32_t provider;
        uint32_t providerVersion;
        uint32_t compressionFormat;
    };
#pragma pack(pop)

    static Result<WofReparsePoint> Parse(BufferView buffer);
    static Result<WofReparsePoint> Parse(WofReparsePoint::Layout& layout);

    WofReparsePoint(const WofReparsePoint::Layout& header);

    uint32_t Version() const;
    WofProviderType Provider() const;
    uint32_t ProviderVersion() const;
    WofAlgorithm CompressionFormat() const;

private:
    uint32_t m_version;
    WofProviderType m_provider;
    uint32_t m_providerVersion;
    WofAlgorithm m_compressionFormat;
};

}  // namespace Ntfs
}  // namespace Orc
