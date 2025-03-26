//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "SystemDetails.h"

#include <boost/scope_exit.hpp>

#include <filesystem>
// Headers for CppUnitTest
#include "CppUnitTest.h"

#include "UnittestHelper.h"

using namespace std;

namespace test = Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Test;

std::wstring UnitTestHelper::GetDirectoryName(const std::wstring& apath)
{
    fs::path spath = apath;

    if (spath.has_parent_path())
        return spath.parent_path().wstring();
    else
        return L"";
}

HRESULT UnitTestHelper::ExtractArchive(
    ArchiveFormat format,
    ArchiveExtract::MakeArchiveStream makeArchiveStream,
    const ArchiveExtract::ItemShouldBeExtractedCallback pShouldBeExtracted,
    ArchiveExtract::MakeOutputStream MakeWriteAbleStream,
    OrcArchive::ArchiveCallback archiveCallback)
{
    HRESULT hr = E_FAIL;

    std::shared_ptr<ArchiveExtract> extractor = ArchiveExtract::MakeExtractor(format, false);
    if (!extractor)
    {
        Log::Error(L"Failed to create extractor");
        return E_FAIL;
    }

    extractor->SetCallback(archiveCallback);

    if (FAILED(extractor->Extract(makeArchiveStream, pShouldBeExtracted, MakeWriteAbleStream)))
    {
        Log::Error(L"Failed to extract archive");
        return hr;
    }

    return S_OK;
}
