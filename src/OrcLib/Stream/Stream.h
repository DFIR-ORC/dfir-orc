//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <memory>

#include "Stream/SeekDirection.h"
#include "Utils/BufferSpan.h"
#include "Utils/BufferView.h"

//
// Lightweight stream interface to replace ByteStream
//

namespace Orc {
namespace Stream {

class Stream
{
public:
    using Ptr = std::shared_ptr<Stream>;

    virtual ~Stream() {}

    virtual size_t Read(BufferSpan output, std::error_code& ec) = 0;
    virtual size_t Write(BufferView& input, std::error_code& ec) = 0;
    virtual uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec) = 0;
};

}  // namespace Stream

using Stream = Stream::Stream;

}  // namespace Orc
