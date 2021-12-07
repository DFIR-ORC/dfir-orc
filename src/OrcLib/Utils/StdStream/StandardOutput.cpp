//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "StandardOutput.h"

namespace Orc {

StandardOutput::StandardOutput()
    : m_coutOriginalFlags(std::cout.flags())
    , m_wcoutOriginalFlags(std::wcout.flags())
    , m_hasTeeRedirection(false)
    , m_fileTee(std::make_shared<StandardOutputFileTee>())
{
    // Forward writes stdout to WriteConsole for performance (if STD_OUTPUT_HANDLE is not already redirected)
    EnableWriteConsoleRedirection();
}

StandardOutput::~StandardOutput()
{
    DisableWriteConsoleRedirection();
    DisableFileTee();

    Reset();
}

void StandardOutput::Reset()
{
    std::cout.flags(m_coutOriginalFlags);
    std::wcout.flags(m_wcoutOriginalFlags);
}

const StandardOutputFileTee& StandardOutput::FileTee() const
{
    return *m_fileTee;
}

std::shared_ptr<StandardOutputFileTee>& StandardOutput::FileTeePtr()
{
    return m_fileTee;
}

void StandardOutput::EnableWriteConsoleRedirection()
{
    if (m_hasTeeRedirection)
    {
        // Tee file should be enabled last or it should be disabled then restored once it has been to installed.
        // When WriteConsoleRedirection is installed last it will not forward to the original output.
        Log::Error(L"Failed to enable stdout file redirection because stdout is already redirected with 'Tee'");
        assert(m_hasTeeRedirection == false);
        return;
    }

    // Ensure current output will be on console otherwise keep using stdout
    DWORD dwConsoleMode = 0;
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dwConsoleMode))
    {
        return;
    }

    m_writeConsoleRedirection.Enable();
}

void StandardOutput::DisableWriteConsoleRedirection()
{
    m_writeConsoleRedirection.Disable();
}

void StandardOutput::OpenTeeFile(
    const std::filesystem::path& path,
    Orc::FileDisposition disposition,
    std::error_code& ec)
{
    m_fileTee->Open(path, disposition, ec);
}

void StandardOutput::EnableFileTee()
{
    m_hasTeeRedirection = true;
    m_fileTee->Enable();
}

void StandardOutput::DisableFileTee()
{
    m_fileTee->Close();
    m_hasTeeRedirection = false;
}

}  // namespace Orc
