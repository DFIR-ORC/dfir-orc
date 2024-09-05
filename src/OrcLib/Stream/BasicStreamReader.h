//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <memory>

#include "Utils/BufferSpan.h"

//
// Lightweight stream interface
//

namespace Orc {
namespace Stream {

class StreamBasicReader
{
public:
    virtual ~StreamBasicReader() {}

    virtual size_t Read(BufferSpan output, std::error_code& ec) = 0;
};

}  // namespace Stream
}  // namespace Orc
