//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <memory>

#include "Stream/SeekDirection.h"
#include "BasicStreamReader.h"

//
// Lightweight stream interface
//

namespace Orc {
namespace Stream {

class StreamReader : public StreamBasicReader
{
public:
    using Ptr = std::shared_ptr<StreamReader>;

    virtual uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec) = 0;
};

}  // namespace Stream

using StreamReader = Stream::StreamReader;

}  // namespace Orc
