//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Archive/IArchive.h"

#include "Archive/CompressionLevel.h"

namespace Orc {

namespace Archive {

class Archive7z final : public IArchive
{
public:
    Archive7z(Format format, CompressionLevel level, std::wstring password);

    void Add(std::unique_ptr<Item> item) override;

    void Add(Items items) override;

    void Compress(const std::shared_ptr<ByteStream>& output, std::error_code& ec) override;

    void Compress(
        const std::shared_ptr<ByteStream>& outputArchive,
        const std::shared_ptr<ByteStream>& inputArchive,
        std::error_code& ec) override;

    // List of added items waiting to be processed by the Compress method
    const Items& AddedItems() const override;

    Archive::CompressionLevel CompressionLevel() const { return m_compressionLevel; }

    void SetCompressionLevel(Archive::CompressionLevel level, std::error_code& ec);

private:
    const Format m_format;
    Archive::CompressionLevel m_compressionLevel;
    const std::wstring m_password;
    Items m_items;
};

}  // namespace Archive

}  // namespace Orc
