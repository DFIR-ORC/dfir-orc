//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include "Stream/SeekDirection.h"
#include "Utils/BufferView.h"
#include "Utils/Guard.h"

namespace Orc {

class FileStreamConcept
{
public:
    using FileHandle = Guard::FileHandle;

    FileStreamConcept(FileHandle&& handle);

    size_t Read(gsl::span<uint8_t> output, std::error_code& ec);
    size_t Write(BufferView input, std::error_code& ec);
    uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec);

private:
    FileHandle m_handle;
};

}  // namespace Orc
