//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <memory>
#include <system_error>
#include <functional>
#include <vector>

namespace Orc {

class ByteStream;

namespace Archive {

class Item;

enum class Format
{
    k7z = 0,
    k7zZip
};

class IArchive
{
public:
    using Items = std::vector<std::unique_ptr<Item>>;
    using CompletionHandler = std::function<void(const std::error_code&)>;

    virtual ~IArchive() {}

    virtual void Add(std::unique_ptr<Item> item) = 0;

    virtual void Add(Items items) = 0;

    virtual void Compress(const std::shared_ptr<ByteStream>& output, std::error_code& ec) = 0;

    virtual void Compress(
        const std::shared_ptr<ByteStream>& outputArchive,
        const std::shared_ptr<ByteStream>& inputArchive,
        std::error_code& ec) = 0;

    // List of added items waiting to be processed by the Compress method
    virtual const Items& AddedItems() const = 0;
};

}  // namespace Archive
}  // namespace Orc
