//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "USNJournalWalkerOffline.h"
#include "ArchiveExtract.h"
#include "FileStream.h"
#include "OfflineMFTReader.h"
#include "TemporaryStream.h"
#include "Temporary.h"
#include "NtfsDataStructures.h"
#include "ByteStream.h"
#include "Utils/BufferSpan.h"
#include "Utils/BufferView.h"
#include "Utils/Guard.h"

#include <string>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
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
            bool shouldReadAnotherChunk = false;
            ULONG64 unrecognizedBytes = 0;

            Assert::IsTrue(
                S_OK
                == USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    unrecognizedBytes));

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
                    unrecognizedBytes));

            Assert::IsTrue(adjustmentOffset == 0);
            Assert::IsTrue((BYTE*)nextUSNRecord == pCurrentChunkPosition + 24);
            Assert::IsTrue(nextUSNRecord->MajorVersion == 0x2);
            Assert::IsTrue(shouldReadAnotherChunk == false);
            Assert::AreEqual(0ULL, (unsigned long long)unrecognizedBytes, L"zero padding is not unrecognized data");

            pCurrentChunkPosition = (BYTE*)nextUSNRecord + nextUSNRecord->RecordLength;

            Assert::IsTrue(
                S_OK
                != USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    unrecognizedBytes));

            Assert::IsTrue(shouldReadAnotherChunk == true);
            Assert::AreEqual(0ULL, (unsigned long long)unrecognizedBytes);
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
            bool shouldReadAnotherChunk = false;
            ULONG64 unrecognizedBytes = 0;

            Assert::IsTrue(
                S_OK
                != USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    unrecognizedBytes));

            // No record in the chunk: the scan passes over the bad header and asks for the bytes it could not check
            // yet, instead of abandoning the journal.
            Assert::IsTrue(nextUSNRecord == nullptr);
            Assert::IsTrue(shouldReadAnotherChunk == true);
            Assert::IsTrue(unrecognizedBytes > 0);
            Assert::IsTrue(adjustmentOffset > 0 && adjustmentOffset < sizeof(usnJrnl));
        }

        // incomplete record
        {
            unsigned char usnJrnl[] = {
                0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x5d, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00};

            BYTE* pCurrentChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl);
            BYTE* pEndChunkPosition = reinterpret_cast<BYTE*>(&usnJrnl) + sizeof(usnJrnl);
            USN_RECORD* nextUSNRecord = nullptr;
            ULONG64 adjustmentOffset = 0;
            bool shouldReadAnotherChunk = false;
            ULONG64 unrecognizedBytes = 0;

            Assert::IsTrue(
                S_OK
                != USNJournalWalkerOffline::FindNextUSNRecord(
                    pCurrentChunkPosition,
                    pEndChunkPosition,
                    (BYTE**)&nextUSNRecord,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    unrecognizedBytes));

            Assert::IsTrue(shouldReadAnotherChunk == true);
            Assert::AreEqual(0ULL, (unsigned long long)unrecognizedBytes);
            Assert::IsTrue(adjustmentOffset == 16);
        }
    }

    // A corrupted stretch between two records costs the bytes of that stretch, not every record after it.
    TEST_METHOD(USNJournalWalkerOfflineResyncsAfterGarbage)
    {
        std::vector<BYTE> journal = MakeUsnRecordV2(L"first.txt");
        const size_t garbageStart = journal.size();
        journal.insert(journal.end(), 13, 0x41);
        journal.insert(journal.end(), 3, 0x00);
        const size_t secondStart = journal.size();
        const std::vector<BYTE> second = MakeUsnRecordV2(L"second.log");
        journal.insert(journal.end(), second.begin(), second.end());

        BYTE* const begin = journal.data();
        BYTE* const end = journal.data() + journal.size();
        USN_RECORD* next = nullptr;
        ULONG64 adjustmentOffset = 0;
        bool shouldReadAnotherChunk = false;
        ULONG64 unrecognizedBytes = 0;

        Assert::AreEqual(
            S_OK,
            USNJournalWalkerOffline::FindNextUSNRecord(
                begin, end, (BYTE**)&next, shouldReadAnotherChunk, adjustmentOffset, unrecognizedBytes));
        Assert::IsTrue((BYTE*)next == begin);

        Assert::AreEqual(
            S_OK,
            USNJournalWalkerOffline::FindNextUSNRecord(
                begin + garbageStart, end, (BYTE**)&next, shouldReadAnotherChunk, adjustmentOffset, unrecognizedBytes));
        Assert::IsTrue((BYTE*)next == begin + secondStart);
        Assert::IsFalse(shouldReadAnotherChunk);
        Assert::AreEqual(13ULL, (unsigned long long)unrecognizedBytes, L"only the non-zero garbage bytes count");
    }

    // A version 2 header is only trusted when its lengths agree: garbage that happens to start with 02 00 must not be
    // taken for a record, whether its RecordLength is too small to leave it or larger than its name.
    TEST_METHOD(USNJournalWalkerOfflineRejectsInconsistentRecordLength)
    {
        for (const DWORD badLength : {DWORD(0x10), DWORD(0x38), DWORD(0x400)})
        {
            std::vector<BYTE> journal = MakeUsnRecordV2(L"bogus.txt");
            reinterpret_cast<USN_RECORD*>(journal.data())->RecordLength = badLength;
            journal.resize(0x60, 0x00);
            const size_t validStart = journal.size();
            const std::vector<BYTE> valid = MakeUsnRecordV2(L"valid.txt");
            journal.insert(journal.end(), valid.begin(), valid.end());

            USN_RECORD* next = nullptr;
            ULONG64 adjustmentOffset = 0;
            bool shouldReadAnotherChunk = false;
            ULONG64 unrecognizedBytes = 0;

            Assert::AreEqual(
                S_OK,
                USNJournalWalkerOffline::FindNextUSNRecord(
                    journal.data(),
                    journal.data() + journal.size(),
                    (BYTE**)&next,
                    shouldReadAnotherChunk,
                    adjustmentOffset,
                    unrecognizedBytes));
            Assert::IsTrue((BYTE*)next == journal.data() + validStart, std::to_wstring(badLength).c_str());
            Assert::IsTrue(unrecognizedBytes > 0);
        }
    }

    // Without an MFT walk the walker has no location filter and names every record, so ReadJournal can run on a
    // journal alone: every record it reads reaches the callback.
    TEST_METHOD(USNJournalWalkerOfflineReadsEveryRecordAcrossChunks)
    {
        const auto journal = MakeJournal(200);
        const auto stream = std::make_shared<FaultyMemoryStream>(journal.bytes);

        std::vector<DWORDLONG> seen;
        Assert::AreEqual(S_OK, ReadWithBufferSize(stream, 0x1000, seen));
        Assert::AreEqual(journal.starts.size(), seen.size());
        for (size_t i = 0; i < seen.size(); ++i)
        {
            Assert::AreEqual(RecordReference(i), seen[i]);
        }
    }

    // A read that fails under the journal - a bad sector, a stream error - costs the sectors that cannot be read,
    // not the rest of the journal: the records before and after them are still reported, and the walk succeeds.
    TEST_METHOD(USNJournalWalkerOfflineSkipsUnreadableSectors)
    {
        const auto journal = MakeJournal(200);
        const auto stream = std::make_shared<FaultyMemoryStream>(journal.bytes);
        stream->badFrom = 5000;
        stream->badTo = 5100;

        std::vector<DWORDLONG> seen;
        Assert::AreEqual(S_OK, ReadWithBufferSize(stream, 0x1000, seen));

        const ULONG64 lostFrom = stream->badFrom - kSectorSize;
        const ULONG64 lostTo = stream->badTo + kSectorSize;
        size_t expectedAtLeast = 0;
        for (size_t i = 0; i < journal.starts.size(); ++i)
        {
            const ULONG64 start = journal.starts[i];
            const ULONG64 end = start + journal.lengths[i];
            if (end <= lostFrom || start >= lostTo)
            {
                ++expectedAtLeast;
                Assert::IsTrue(
                    std::find(seen.begin(), seen.end(), RecordReference(i)) != seen.end(),
                    (L"record " + std::to_wstring(i) + L" away from the bad sectors was not read").c_str());
            }
        }

        Assert::IsTrue(seen.size() >= expectedAtLeast);
        Assert::IsTrue(seen.size() < journal.starts.size(), L"records in the unreadable bytes cannot be reported");
        Assert::AreEqual(RecordReference(journal.starts.size() - 1), seen.back(), L"the walk must reach the end");
    }

    // A seek that fails is a read that fails: the same sectors are given up and the walk goes on.
    TEST_METHOD(USNJournalWalkerOfflineSkipsUnseekableSectors)
    {
        const auto journal = MakeJournal(200);
        const auto stream = std::make_shared<FaultyMemoryStream>(journal.bytes);
        stream->badFrom = 9000;
        stream->badTo = 9001;
        stream->failSeeks = true;

        std::vector<DWORDLONG> seen;
        Assert::AreEqual(S_OK, ReadWithBufferSize(stream, 0x1000, seen));
        Assert::IsTrue(seen.size() < journal.starts.size());
        Assert::AreEqual(RecordReference(journal.starts.size() - 1), seen.back());
        Assert::AreEqual(RecordReference(0), seen.front());
    }

    // Nothing read is not a best-effort result: the walk reports the read error.
    TEST_METHOD(USNJournalWalkerOfflineFailsWhenNothingCanBeRead)
    {
        const auto journal = MakeJournal(50);

        const auto unreadable = std::make_shared<FaultyMemoryStream>(journal.bytes);
        unreadable->badFrom = 0;
        unreadable->badTo = journal.bytes.size();
        std::vector<DWORDLONG> seen;
        Assert::IsTrue(FAILED(ReadWithBufferSize(unreadable, 0x1000, seen)));
        Assert::IsTrue(seen.empty());

        const auto refused = std::make_shared<FaultyMemoryStream>(journal.bytes);
        refused->readable = false;
        Assert::IsTrue(FAILED(ReadWithBufferSize(refused, 0x1000, seen)));
        Assert::IsTrue(seen.empty());
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
    typedef std::map<int, OrcArchive::ArchiveItem> ITEMS;
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

        std::for_each(std::begin(m_Items), std::end(m_Items), [](std::pair<const int, OrcArchive::ArchiveItem>& item) {
            item.second.Stream.reset();

            if (!item.second.Path.empty())
            {
                DeleteFile(item.second.Path.c_str());
            }
        });
    }

    static constexpr ULONG64 kSectorSize = 512;

    // An in-memory journal whose reads fail when they touch [badFrom, badTo), and whose seeks into that range fail
    // too when 'failSeeks' is set.
    class FaultyMemoryStream : public ByteStream
    {
    public:
        explicit FaultyMemoryStream(std::vector<BYTE> data)
            : m_data(std::move(data))
        {
        }

        ULONG64 badFrom = 0;
        ULONG64 badTo = 0;
        bool failSeeks = false;
        bool readable = true;

        STDMETHOD(IsOpen)() override { return S_OK; }
        STDMETHOD(CanRead)() override { return readable ? S_OK : S_FALSE; }
        STDMETHOD(CanWrite)() override { return S_FALSE; }
        STDMETHOD(CanSeek)() override { return S_OK; }

        STDMETHOD(SetFilePointer)(LONGLONG distance, DWORD moveMethod, PULONG64 currentPointer) override
        {
            if (moveMethod != FILE_BEGIN || distance < 0)
            {
                return E_INVALIDARG;
            }

            if (failSeeks && (ULONG64)distance >= badFrom && (ULONG64)distance < badTo)
            {
                return HRESULT_FROM_WIN32(ERROR_SEEK);
            }

            m_position = (ULONG64)distance;
            if (currentPointer)
            {
                *currentPointer = m_position;
            }

            return S_OK;
        }

        STDMETHOD_(ULONG64, GetSize)() override { return m_data.size(); }
        STDMETHOD(SetSize)(ULONG64) override { return E_NOTIMPL; }
        STDMETHOD(Close)() override { return S_OK; }

    protected:
        STDMETHOD(Read_)(PVOID buffer, ULONGLONG bytes, PULONGLONG bytesRead) override
        {
            *bytesRead = 0;
            const ULONG64 end = (std::min<ULONG64>)(m_position + bytes, m_data.size());
            if (m_position < badTo && end > badFrom)
            {
                return HRESULT_FROM_WIN32(ERROR_CRC);
            }

            if (m_position < end)
            {
                // Both sides of the copy are spans: the source is sliced out of the backing bytes and the
                // destination is bounded by the length the caller passed, so a fixture that miscounted
                // would fail here rather than silently overrun the caller's buffer mid-test.
                const size_t count = static_cast<size_t>(end - m_position);
                const BufferView source =
                    MakeBufferView(m_data).subspan(static_cast<size_t>(m_position), count);
                const BufferSpan destination(reinterpret_cast<uint8_t*>(buffer), static_cast<size_t>(bytes));

                std::copy(source.begin(), source.end(), destination.begin());
                *bytesRead = count;
                m_position = end;
            }

            return S_OK;
        }

        STDMETHOD(Write_)(const PVOID, ULONGLONG, PULONGLONG) override { return E_NOTIMPL; }

    private:
        std::vector<BYTE> m_data;
        ULONG64 m_position = 0;
    };

    struct Journal
    {
        std::vector<BYTE> bytes;
        std::vector<ULONG64> starts;
        std::vector<ULONG64> lengths;
    };

    static DWORDLONG RecordReference(size_t index) { return 0x0001000000000000ULL + index + 1; }

    // 'count' records one after the other, with names of different lengths so that records do not line up on chunk
    // or sector boundaries. Record i has the file reference RecordReference(i).
    static Journal MakeJournal(size_t count)
    {
        Journal journal;
        for (size_t i = 0; i < count; ++i)
        {
            std::vector<BYTE> record = MakeUsnRecordV2(std::wstring(1 + (i % 23), L'a' + (wchar_t)(i % 26)));
            reinterpret_cast<USN_RECORD*>(record.data())->FileReferenceNumber = RecordReference(i);
            journal.starts.push_back(journal.bytes.size());
            journal.lengths.push_back(record.size());
            journal.bytes.insert(journal.bytes.end(), record.begin(), record.end());
        }

        return journal;
    }

    // ReadJournal over 'stream' with the given chunk size, collecting the file reference of every reported record.
    static HRESULT ReadWithBufferSize(
        const std::shared_ptr<ByteStream>& stream,
        DWORD bufferSize,
        std::vector<DWORDLONG>& seen)
    {
        const DWORD previous = USNJournalWalkerOffline::GetBufferSize();
        USNJournalWalkerOffline::SetBufferSize(bufferSize);
        const auto restore = Guard::CreateScopeGuard([previous]() { USNJournalWalkerOffline::SetBufferSize(previous); });

        seen.clear();
        USNJournalWalkerOffline walker;
        walker.SetUsnJournal(stream);

        IUSNJournalWalker::Callbacks callbacks;
        callbacks.RecordCallback = [&seen](const std::shared_ptr<VolumeReader>&, WCHAR*, USN_RECORD* record) {
            seen.push_back(record->FileReferenceNumber);
        };

        return walker.ReadJournal(callbacks);
    }

    // A well-formed USN_RECORD_V2 for 'name', padded to 8 bytes as NTFS writes it.
    static std::vector<BYTE> MakeUsnRecordV2(const std::wstring& name)
    {
        const DWORD nameBytes = static_cast<DWORD>(name.size() * sizeof(WCHAR));
        const DWORD fixed = FIELD_OFFSET(USN_RECORD, FileName);
        const DWORD length = (fixed + nameBytes + 7) & ~DWORD(7);

        std::vector<BYTE> bytes(length, 0);
        auto* record = reinterpret_cast<USN_RECORD*>(bytes.data());
        record->RecordLength = length;
        record->MajorVersion = 2;
        record->MinorVersion = 0;
        record->FileReferenceNumber = 0x0001000000000123ULL;
        record->ParentFileReferenceNumber = 0x0007000000000040ULL;  // not the root: no volume name to prefix
        record->Usn = 0x1000;
        record->TimeStamp.QuadPart = 0x01d12b79bc8a394eLL;
        record->Reason = USN_REASON_FILE_CREATE;
        record->FileAttributes = FILE_ATTRIBUTE_ARCHIVE;
        record->FileNameLength = static_cast<WORD>(nameBytes);
        record->FileNameOffset = static_cast<WORD>(fixed);

        const BufferSpan nameField = BufferSpan(bytes.data(), bytes.size()).subspan(fixed, nameBytes);
        std::copy_n(reinterpret_cast<const uint8_t*>(name.data()), nameBytes, nameField.begin());
        return bytes;
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
                    Log::Error("Failed to open temporary archive (code: {:#x})", hr);
                    return nullptr;
                }

                return pStream;
            }
        };

        auto ArchiveCallback = [this](const OrcArchive::ArchiveItem& item) {
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
