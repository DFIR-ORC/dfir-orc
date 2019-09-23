//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <string>

#include "ArchiveExtract.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

namespace Test {

class UnitTestHelper
{
public:
    UnitTestHelper() {};

    void UnitTestHelper::InitLogFileWriter(const logger& pLog);
    void UnitTestHelper::FinalizeLogFileWriter(const logger& pLog);

    std::wstring GetDirectoryName(const std::wstring& path);

    HRESULT ExtractArchive(
        const logger& pLog,
        ArchiveFormat format,
        ArchiveExtract::MakeArchiveStream makeArchiveStream,
        const ArchiveExtract::ItemShouldBeExtractedCallback pShouldBeExtracted,
        ArchiveExtract::MakeOutputStream MakeWriteAbleStream,
        Archive::ArchiveCallback archiveCallback);

private:
    std::wstring m_strAccumulator;
};
}  // namespace Test

}  // namespace Orc

#ifndef __WFILE__
#    define WIDEN2(x) L##x
#    define WIDEN(x) WIDEN2(x)
#    define __WFILE__ WIDEN(__FILE__)
#endif  // !

#pragma managed(pop)