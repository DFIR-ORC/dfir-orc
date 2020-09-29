//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "FatWalker.h"
#include "FileStream.h"
#include "PartitionTable.h"
#include "Partition.h"
#include "TemporaryStream.h"
#include "Temporary.h"
#include "Location.h"
#include "VolumeReader.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(FatWalkerTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(Fat12WalkerBasicTest)
    {
        m_NbFiles = 0;
        m_NbFolders = 0;
        ProcessArchive(helper.GetDirectoryName(__WFILE__) + L"\\fat_images\\fat12.7z");

        Assert::IsTrue(m_NbFiles == 0x3E2);  // (files A,B,C,D.txt + Plop.txt + desktop.ini)
        Assert::IsTrue(m_NbFolders == 0x5);  // (3 folders + recycle bin + system volume information)

        DeleteFile(m_ArchiveItem.Path.c_str());
    }

    TEST_METHOD(Fat16WalkerBasicTest)
    {
        m_NbFiles = 0;
        m_NbFolders = 0;
        ProcessArchive(helper.GetDirectoryName(__WFILE__) + L"\\fat_images\\fat16.7z");

        Assert::IsTrue(m_NbFiles == 0x601);
        Assert::IsTrue(m_NbFolders == 0xBD4);

        DeleteFile(m_ArchiveItem.Path.c_str());
    }

    TEST_METHOD(Fat32WalkerBasicTest)
    {
        m_NbFiles = 0;
        m_NbFolders = 0;
        ProcessArchive(helper.GetDirectoryName(__WFILE__) + L"\\fat_images\\fat32.7z");

        Assert::IsTrue(m_NbFiles == 0x5D6);
        Assert::IsTrue(m_NbFolders == 0x6);  // (4 folders + recycle bin + system volume information)

        DeleteFile(m_ArchiveItem.Path.c_str());
    }

private:
    DWORD64 m_NbFiles;
    DWORD64 m_NbFolders;
    Archive::ArchiveItem m_ArchiveItem;

    void ProcessArchive(const std::wstring& archive)
    {
        // first extract archive
        LPCWSTR archiveStr = archive.c_str();

        Assert::IsTrue(S_OK == ExtractArchive(archiveStr));

        std::shared_ptr<ByteStream>& fatImageStream = m_ArchiveItem.Stream;
        LPCWSTR fatImage = m_ArchiveItem.Path.c_str();

        PartitionTable pt;
        Assert::IsTrue(S_OK == pt.LoadPartitionTable(fatImage));
        Assert::IsTrue(1 == pt.Table().size());
        const Partition& p(pt.Table()[0]);

        std::wstringstream ss;
        ss << std::wstring(fatImage);
        ss << L",part=1";

        std::shared_ptr<Location> loc = std::make_shared<Location>(ss.str(), Location::Type::ImageFileDisk);
        std::shared_ptr<VolumeReader> volReader = loc->GetReader();

        // update reader
        Assert::IsTrue(S_OK == volReader->LoadDiskProperties());

        FatWalker::Callbacks callBacks;
        callBacks.m_FileEntryCall = [this](
                                        const std::shared_ptr<VolumeReader>& volreader,
                                        const WCHAR* szFullName,
                                        const std::shared_ptr<FatFileEntry>& fileEntry) {
            if (fileEntry->IsFolder())
                m_NbFolders++;
            else
                m_NbFiles++;
        };

        FatWalker walker;
        walker.Init(loc, false);
        walker.Process(callBacks);

        fatImageStream->Close();
    }

    HRESULT ExtractArchive(LPCWSTR archive)
    {
        auto MakeArchiveStream = [archive](std::shared_ptr<ByteStream>& stream) -> HRESULT {
            HRESULT hr = E_FAIL;

            std::shared_ptr<FileStream> fs(std::make_shared<FileStream>());
            fs->ReadFrom(archive);

            if (FAILED(fs->IsOpen()))
                return hr;

            stream = fs;

            return S_OK;
        };

        auto ShouldItemBeExtracted = [](const std::wstring& strNameInArchive) -> bool { return true; };

        auto MakeWriteStream = [this](Archive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
            HRESULT hr = E_FAIL;

            WCHAR szTempDir[MAX_PATH];
            if (FAILED(UtilGetTempDirPath(szTempDir, MAX_PATH)))
                return nullptr;

            if (FAILED(UtilGetUniquePath(szTempDir, item.NameInArchive.c_str(), item.Path)))
                return nullptr;

            auto pStream = std::make_shared<FileStream>();
            pStream->OpenFile(item.Path.c_str(), GENERIC_WRITE | GENERIC_READ, 0L, NULL, CREATE_ALWAYS, 0L, NULL);

            return pStream;
        };

        auto ArchiveCallback = [this](const Archive::ArchiveItem& item) { m_ArchiveItem = item; };

        return helper.ExtractArchive(
            ArchiveFormat::SevenZip, MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream, ArchiveCallback);
    }
};
}  // namespace Orc::Test
