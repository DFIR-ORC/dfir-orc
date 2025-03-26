//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "PartitionTable.h"
#include "Partition.h"
#include "Location.h"
#include "MFTWalker.h"
#include "FileStream.h"
#include "TemporaryStream.h"
#include "Temporary.h"
#include "MFTRecordFileInfo.h"
#include "BinaryBuffer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(MFTWalkerTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(MFTWalkerBasicTest)
    {
        m_NbFiles = 0;
        m_NbFolders = 0;
        ProcessArchive(helper.GetDirectoryName(__WFILE__) + L"\\ntfs_images\\ntfs.7z");

        Assert::IsTrue(m_NbFiles == 0x16);
        Assert::IsTrue(m_NbFolders == 0x9);

        DeleteFile(m_ArchiveItem.Path.c_str());
    };

private:
    DWORD64 m_NbFiles;
    DWORD64 m_NbFolders;
    OrcArchive::ArchiveItem m_ArchiveItem;

    void ProcessArchive(const std::wstring& archive)
    {
        // first extract archive
        LPCWSTR archiveStr = archive.c_str();

        Assert::IsTrue(S_OK == ExtractArchive(archiveStr));

        std::shared_ptr<ByteStream>& ntfsImageStream = m_ArchiveItem.Stream;
        LPCWSTR ntfsImage = m_ArchiveItem.Path.c_str();

        PartitionTable pt;
        Assert::IsTrue(S_OK == pt.LoadPartitionTable(ntfsImage));
        Assert::IsTrue(1 == pt.Table().size());
        const Partition& p(pt.Table()[0]);

        std::wstringstream ss;
        ss << std::wstring(ntfsImage);
        ss << L",part=1";

        std::shared_ptr<Location> loc = std::make_shared<Location>(ss.str(), Location::Type::ImageFileDisk);
        std::shared_ptr<VolumeReader> volReader = loc->GetReader();

        // update reader
        Assert::IsTrue(S_OK == volReader->LoadDiskProperties());

        MFTWalker::Callbacks callBacks;
        MFTWalker walker;

        callBacks.FileNameAndDataCallback = [this, &walker](
                                                const std::shared_ptr<VolumeReader>& volreader,
                                                MFTRecord* pElt,
                                                const PFILE_NAME pFileName,
                                                const std::shared_ptr<DataAttribute>& pDataAttr) {
            using namespace std::string_literals;

            std::vector<Filter> empty;
            Authenticode authenticode;
            MFTRecordFileInfo fi(
                L"Test"s,
                volreader,
                Intentions::FILEINFO_ALL,
                empty,
                walker.GetFullNameBuilder()(pFileName, pDataAttr),
                pElt,
                pFileName,
                pDataAttr,
                authenticode);

            fi.CheckHash();

            std::wstring fileName = pFileName->FileName;
            if (!wmemcmp(fileName.c_str(), L"notepad.exe", fileName.size() - 1))
            {
                unsigned char sha1[20] = {0x80, 0x07, 0x18, 0x6A, 0xB2, 0xB7, 0x1C, 0x48, 0x2E, 0xA2,
                                          0xC4, 0xBC, 0x43, 0x04, 0xB2, 0x4B, 0xDC, 0x68, 0x34, 0xEC};

                Assert::IsTrue(!memcmp(fi.GetDetails()->SHA1().GetData(), sha1, sizeof(sha1)));
            }

            m_NbFiles++;
        };

        callBacks.DirectoryCallback = [this](
                                          const std::shared_ptr<VolumeReader>& volreader,
                                          MFTRecord* pElt,
                                          const PFILE_NAME pFileName,
                                          const std::shared_ptr<IndexAllocationAttribute>& pAttr) { m_NbFolders++; };

        Assert::IsTrue(S_OK == walker.Initialize(loc, ResurrectRecordsMode::kNo));
        Assert::IsTrue(S_OK == walker.Walk(callBacks));

        ntfsImageStream->Close();
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

        auto MakeWriteStream = [this](OrcArchive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
            HRESULT hr = E_FAIL;

            WCHAR szTempDir[ORC_MAX_PATH];
            if (FAILED(UtilGetTempDirPath(szTempDir, ORC_MAX_PATH)))
                return nullptr;

            if (FAILED(UtilGetUniquePath(szTempDir, item.NameInArchive.c_str(), item.Path)))
                return nullptr;

            auto pStream = std::make_shared<FileStream>();
            pStream->OpenFile(item.Path.c_str(), GENERIC_WRITE | GENERIC_READ, 0L, NULL, CREATE_ALWAYS, 0L, NULL);

            return pStream;
        };

        auto ArchiveCallback = [this](const OrcArchive::ArchiveItem& item) { m_ArchiveItem = item; };

        return helper.ExtractArchive(
            ArchiveFormat::SevenZip, MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream, ArchiveCallback);
    }
};
}  // namespace Orc::Test
