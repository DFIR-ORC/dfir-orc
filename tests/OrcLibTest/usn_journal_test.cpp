//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "USNJournalWalkerOffline.h"
#include "ArchiveExtract.h"
#include "FileStream.h"
#include "OfflineMFTReader.h"
#include "ImportItem.h"
#include "TemporaryStream.h"
#include "Temporary.h"
#include "NtfsDataStructures.h"

#include <string>
#include <memory>
#include <algorithm>
#include <boost/algorithm/string.hpp>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(USNJournalTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(USNJournalWalkerOfflineBasicTest)
    {
        // in this example there is a gap (filled out with zeros) between 2 records
        {
            unsigned char usnJrnl[] = {
                0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x5d, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x5b,
                0x2c, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x98, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4e, 0x39,
                0x8a, 0xbc, 0x79, 0x2b, 0xd1, 0x01, 0x00, 0x08, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x64, 0x01, 0x00,
                0x00, 0x20, 0x00, 0x00, 0x00, 0x12, 0x00, 0x3c, 0x00, 0x65, 0x00, 0x73, 0x00, 0x73, 0x00, 0x61, 0x00,
                0x69, 0x00, 0x2e, 0x00, 0x74, 0x00, 0x78, 0x00, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xee, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x74, 0x59, 0x81, 0xde, 0x79, 0x2b, 0xd1, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23,
                0x01, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x14, 0x00, 0x3c, 0x00, 0x73, 0x00, 0x79, 0x00, 0x73, 0x00,
                0x74, 0x00, 0x65, 0x00, 0x6d, 0x00, 0x2e, 0x00, 0x4c, 0x00, 0x4f, 0x00, 0x47, 0x00};

            BYTE* pCurrentChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl);
            BYTE* pEndChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl) + sizeof(usnJrnl);
            USN_RECORD* nextUSNRecord = nullptr;
            ULONG64 adjustmentOffset = 0;
            bool shouldReadAnotherChunk = false, shouldStop = false;

            Assert::IsTrue(
                S_OK
                == USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    shouldStop));

            Assert::IsTrue((BYTE*)nextUSNRecord == pCurrentChunkPosition);

            pCurrentChunkPosition = (BYTE*)nextUSNRecord + nextUSNRecord->RecordLength;

            Assert::IsTrue(
                S_OK
                == USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    shouldStop));

            Assert::IsTrue(adjustmentOffset == 0);
            Assert::IsTrue((BYTE*)nextUSNRecord == pCurrentChunkPosition + 24);
            Assert::IsTrue(nextUSNRecord->MajorVersion == 0x2);
            Assert::IsTrue(shouldReadAnotherChunk == false);
            Assert::IsTrue(shouldStop == false);

            pCurrentChunkPosition = (BYTE*)nextUSNRecord + nextUSNRecord->RecordLength;

            Assert::IsTrue(
                S_OK
                != USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    shouldStop));

            Assert::IsTrue(shouldReadAnotherChunk == true);
            Assert::IsTrue(shouldStop == false);
        }

        // bad record
        {
            unsigned char usnJrnl[] = {
                0x50, 0x00, 0x00, 0x00, 0xCC, 0x00, 0x00, 0x00, 0x5d, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
                0x5b, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x98, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x4e, 0x39, 0x8a, 0xbc, 0x79, 0x2b, 0xd1, 0x01, 0x00, 0x08, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
                0x64, 0x01, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x12, 0x00, 0x3c, 0x00, 0x65, 0x00, 0x73, 0x00,
                0x73, 0x00, 0x61, 0x00, 0x69, 0x00, 0x2e, 0x00, 0x74, 0x00, 0x78, 0x00, 0x74, 0x00, 0x00, 0x00};

            BYTE* pCurrentChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl);
            BYTE* pEndChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl) + sizeof(usnJrnl);
            USN_RECORD* nextUSNRecord = nullptr;
            ULONG64 adjustmentOffset = 0;
            bool shouldReadAnotherChunk = false, shouldStop = false;

            Assert::IsTrue(
                S_OK
                != USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    shouldStop));

            Assert::IsTrue(shouldReadAnotherChunk == false);
            Assert::IsTrue(shouldStop == true);
        }

        // incomplete record
        {
            unsigned char usnJrnl[] = {
                0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x5d, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00};

            BYTE* pCurrentChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl);
            BYTE* pEndChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl) + sizeof(usnJrnl);
            USN_RECORD* nextUSNRecord = nullptr;
            ULONG64 adjustmentOffset = 0;
            bool shouldReadAnotherChunk = false, shouldStop = false;

            Assert::IsTrue(
                S_OK
                != USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    shouldStop));

            Assert::IsTrue(shouldReadAnotherChunk == true);
            Assert::IsTrue(shouldStop == false);
            Assert::IsTrue(adjustmentOffset == 16);
        }
    }

    TEST_METHOD(USNJournalWalkerOfflineDifferentOsTest)
    {
        // WIN XP
        {
            m_NbRecords = 0;
            ProcessArchive(helper.GetDirectoryName(__WFILE__) + L"\\usn_journal\\winxp.7z");
            Assert::IsTrue(m_NbRecords == 0x3e);
        }

        // WIN 7
        {
            USNJournalWalkerOffline::SetBufferSize(512);
            m_NbRecords = 0;
            ProcessArchive(helper.GetDirectoryName(__WFILE__) + L"\\usn_journal\\win7.7z");
            Assert::IsTrue(m_NbRecords == 0x947E);
        }

        // WIN 10
        {
            USNJournalWalkerOffline::SetBufferSize(2018);
            m_NbRecords = 0;
            ProcessArchive(helper.GetDirectoryName(__WFILE__) + L"\\usn_journal\\win10.7z");
            Assert::IsTrue(m_NbRecords == 0xA34F);
        }
    }

private:
    DWORD64 m_NbRecords;
    typedef std::map<int, Archive::ArchiveItem> ITEMS;
    typedef std::map<int, std::wstring> ITEM_PATHS;
    ITEMS m_Items;

    void ProcessArchive(const std::wstring& archive)
    {
        // first extract archive
        LPCWSTR archiveStr = archive.c_str();

        Assert::AreEqual(ExtractArchive(archiveStr), S_OK);

        // load VBR
        CBinaryBuffer buffer;
        buffer.SetCount(sizeof(PackedGenBootSector));
        ULONGLONG dwBytesRead;

        std::shared_ptr<ByteStream>& vbrStream = m_Items[0].Stream;
        Assert::AreEqual(vbrStream->SetFilePointer(0LL, FILE_BEGIN, NULL), S_OK);
        Assert::AreEqual(vbrStream->Read(buffer.GetData(), sizeof(PackedGenBootSector), &dwBytesRead), S_OK);

        // load MFT
        LPCWSTR mft = m_Items[1].Path.c_str();

        std::shared_ptr<Location> loc = std::make_shared<Location>(mft, Location::Type::OfflineMFT);

        {
            std::shared_ptr<VolumeReader> volReader = loc->GetReader();

            // update reader
            Assert::AreEqual(volReader->LoadDiskProperties(), S_OK);

            (std::dynamic_pointer_cast<OfflineMFTReader>(volReader))->SetCharacteristics(buffer);
        }
        {
            // this local scope ensures that the volreader (and streams) are closed before we delete the temp files

            // initialize walker that will parse MFT
            USNJournalWalkerOffline walker;

            Assert::AreEqual(walker.Initialize(loc), S_OK);
            loc.reset();

            // parse MFT
            IUSNJournalWalker::Callbacks callbacks;
            callbacks.RecordCallback =
                [](const std::shared_ptr<VolumeReader>& volreader, WCHAR* szFullName, USN_RECORD* pElt) {};
            Assert::AreEqual(walker.EnumJournal(callbacks), S_OK);

            // load USN Journal
            std::shared_ptr<ByteStream>& usnStream = m_Items[2].Stream;
            Assert::AreEqual(usnStream->SetFilePointer(0LL, FILE_BEGIN, NULL), S_OK);

            walker.SetUsnJournal(usnStream);
            Assert::IsTrue(walker.GetUsnJournal() != nullptr);

            // parse USN journal
            callbacks.RecordCallback =
                [this](const std::shared_ptr<VolumeReader>& volreader, WCHAR* szFullName, USN_RECORD* pElt) {
                    ++m_NbRecords;
                };

            Assert::AreEqual(walker.ReadJournal(callbacks), S_OK);
            // final
            Assert::AreEqual(vbrStream->Close(), S_OK);
            vbrStream.reset();

            Assert::AreEqual(usnStream->Close(), S_OK);
            usnStream.reset();
        }

        std::for_each(std::begin(m_Items), std::end(m_Items), [](std::pair<const int, Archive::ArchiveItem>& item) {
            item.second.Stream.reset();

            if (!item.second.Path.empty())
            {
                DeleteFile(item.second.Path.c_str());
            }
        });
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

            std::size_t found = item.NameInArchive.find(L"mft");
            if (found != std::string::npos)
            {
                if (FAILED(UtilGetUniquePath(szTempDir, item.NameInArchive.c_str(), item.Path)))
                    return nullptr;

                auto pStream = std::make_shared<FileStream>();
                pStream->OpenFile(item.Path.c_str(), GENERIC_WRITE | GENERIC_READ, 0L, NULL, CREATE_ALWAYS, 0L, NULL);

                return pStream;
            }
            else
            {
                auto pStream = std::make_shared<TemporaryStream>();

                if (FAILED(hr = pStream->Open(szTempDir, item.NameInArchive, 1 * 1024 * 1024, false)))
                {
                    spdlog::error("Failed to open temporary archive (code: {:#x})", hr);
                    return nullptr;
                }

                return pStream;
            }
        };

        auto ArchiveCallback = [this](const Archive::ArchiveItem& item) {
            std::size_t found = item.NameInArchive.find(L"vbr");
            if (found != std::string::npos)
            {
                m_Items[0] = item;
                return;
            }

            found = item.NameInArchive.find(L"mft");
            if (found != std::string::npos)
            {
                m_Items[1] = item;
                return;
            }

            found = item.NameInArchive.find(L"usn");
            if (found != std::string::npos)
            {
                m_Items[2] = item;
                return;
            }
        };

        return helper.ExtractArchive(
            ArchiveFormat::SevenZip, MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream, ArchiveCallback);
    }
};
}  // namespace Orc::Test
