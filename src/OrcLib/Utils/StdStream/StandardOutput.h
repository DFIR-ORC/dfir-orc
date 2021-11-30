//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <iostream>

#include "Utils/StdStream/StandardOutputFileTee.h"
#include "Utils/StdStream/StandardOutputWriteConsoleRedirection.h"

namespace Orc {

// This is a wrapper class ensuring that 'StandardOutputFileTee' and 'StandardOutputWriteConsoleRedirection' are
// correctly used together as one could prevent the other to work because of an existing redirection.

class StandardOutput final
{
public:
    StandardOutput();
    ~StandardOutput();

    void EnableWriteConsoleRedirection();
    void DisableWriteConsoleRedirection();

    void EnableFileTee();
    void DisableFileTee();

    void OpenTeeFile(const std::filesystem::path& path, Orc::FileDisposition disposition, std::error_code& ec);

    const StandardOutputFileTee& FileTee() const;

    std::shared_ptr<StandardOutputFileTee>& FileTeePtr();

private:
    void Reset();

    const std::ios_base::fmtflags m_coutOriginalFlags;
    const std::ios_base::fmtflags m_wcoutOriginalFlags;
    bool m_hasTeeRedirection;
    std::shared_ptr<StandardOutputFileTee> m_fileTee;  // need a smart pointer because log sink will also own stream
    StandardOutputWriteConsoleRedirection m_writeConsoleRedirection;
};

}  // namespace Orc
