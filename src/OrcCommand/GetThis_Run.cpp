//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

#include "LogFileWriter.h"
#include "TableOutput.h"
#include "CsvFileWriter.h"
#include "ConfigFileReader.h"
#include "FileFind.h"
#include "ByteStream.h"
#include "FileStream.h"
#include "MemoryStream.h"
#include "TemporaryStream.h"
#include "DevNullStream.h"
#include "StringsStream.h"
#include "XORStream.h"
#include "CryptoHashStream.h"
#include "ParameterCheck.h"
#include "ArchiveExtract.h"

#include "SnapshotVolumeReader.h"

#include "SystemDetails.h"

#include <string>
#include <memory>
#include <filesystem>
#include <sstream>

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Command::GetThis;

std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
Main::CreateOutputDirLogFileAndCSV(const std::wstring& strOutputDir)
{
    HRESULT hr = E_FAIL;

    if (strOutputDir.empty())
        return {E_POINTER, nullptr};

    fs::path outDir(strOutputDir);

    switch (fs::status(outDir).type())
    {
        case fs::file_type::not_found:
            if (create_directories(outDir))
            {
                log::Verbose(_L_, L"Created output directory %s", strOutputDir.c_str());
            }
            else
            {
                log::Verbose(_L_, L"Output directory %s exists", strOutputDir.c_str());
            }
            break;
        case fs::file_type::directory:
            log::Verbose(_L_, L"Specified output directory %s exists and is a directory\r\n", strOutputDir.c_str());
            break;
        default:
            log::Error(
                _L_,
                E_INVALIDARG,
                L"Specified output directory %s exists and is not a directory\r\n",
                strOutputDir.c_str());
            break;
    }

    fs::path logFile;

    logFile = outDir;
    logFile /= L"GetThis.log";

    if (!_L_->IsLoggingToFile())
    {
        if (FAILED(hr = _L_->LogToFile(logFile.wstring().c_str())))
            return {hr, nullptr};
    }

    fs::path csvFile(outDir);
    csvFile /= L"GetThis.csv";

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;
    auto CSV = TableOutput::CSV::Writer::MakeNew(_L_, std::move(options));

    if (FAILED(hr = CSV->WriteToFile(csvFile.wstring().c_str())))
        return {hr, nullptr};

    CSV->SetSchema(config.Output.Schema);

    return {S_OK, std::move(CSV)};
}

std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
Main::CreateArchiveLogFileAndCSV(const std::wstring& pArchivePath, const std::shared_ptr<ArchiveCreate>& compressor)
{
    HRESULT hr = E_FAIL;

    if (pArchivePath.empty())
        return {E_POINTER, nullptr};

    fs::path tempdir;
    tempdir = fs::path(pArchivePath).parent_path();

    auto stream_log = std::make_shared<LogFileWriter>(0x1000);
    stream_log->SetConsoleLog(_L_->ConsoleLog());
    stream_log->SetDebugLog(_L_->DebugLog());
    stream_log->SetVerboseLog(_L_->VerboseLog());

    auto logStream = std::make_shared<TemporaryStream>(stream_log);

    if (FAILED(hr = logStream->Open(tempdir.wstring(), L"GetThisLogStream", 5 * 1024 * 1024)))
    {
        log::Error(_L_, hr, L"Failed to create temp stream\r\n");
        return {hr, nullptr};
    }

    if (FAILED(hr = _L_->LogToStream(logStream)))
    {
        log::Error(_L_, hr, L"Failed to initialize temp logging\r\n");
        return {hr, nullptr};
    }

    auto csvStream = std::make_shared<TemporaryStream>(_L_);

    if (FAILED(hr = csvStream->Open(tempdir.wstring(), L"GetThisCsvStream", 1 * 1024 * 1024)))
    {
        log::Error(_L_, hr, L"Failed to create temp stream\r\n");
        return {hr, nullptr};
    }

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;

    auto CSV = TableOutput::CSV::Writer::MakeNew(_L_, std::move(options));
    if (FAILED(hr = CSV->WriteToStream(csvStream)))
    {
        log::Error(_L_, hr, L"Failed to initialize CSV stream\r\n");
        return {hr, nullptr};
    }

    CSV->SetSchema(config.Output.Schema);

    if (FAILED(hr = compressor->InitArchive(pArchivePath.c_str())))
    {
        log::Error(_L_, hr, L"Failed to initialize archive file %s\r\n", pArchivePath.c_str());
        return {hr, nullptr};
    }

    if (!config.Output.Password.empty())
    {
        if (FAILED(hr = compressor->SetPassword(config.Output.Password)))
        {
            log::Error(_L_, hr, L"Failed to set password for archive file %s\r\n", pArchivePath.c_str());
            return {hr, nullptr};
        }
    }

    return {S_OK, std::move(CSV)};
}

HRESULT Main::RegFlushKeys()
{
    bool bSuccess = true;
    DWORD dwGLE = 0L;

    log::Info(_L_, L"\r\nFlushing HKEY_LOCAL_MACHINE\r\n");
    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
        bSuccess = false;

    log::Info(_L_, L"Flushing HKEY_USERS\r\n");
    dwGLE = RegFlushKey(HKEY_USERS);
    if (dwGLE != ERROR_SUCCESS)
        bSuccess = false;

    if (!bSuccess)
        return HRESULT_FROM_WIN32(dwGLE);
    return S_OK;
}

HRESULT Main::CreateSampleFileName(
    const ContentSpec& content,
    const PFILE_NAME pFileName,
    const wstring& DataName,
    DWORD idx,
    wstring& SampleFileName)
{
    if (pFileName == NULL)
        return E_POINTER;

    WCHAR tmpName[MAX_PATH];
    WCHAR* pContent = NULL;

    switch (content.Type)
    {
        case DATA:
            pContent = L"data";
            break;
        case STRINGS:
            pContent = L"strings";
            break;
        case RAW:
            pContent = L"raw";
            break;
        default:
            pContent = L"";
            break;
    }

    if (idx)
    {
        if (DataName.size())
            swprintf_s(
                tmpName,
                MAX_PATH,
                L"%*.*X%*.*X%*.*X_%.*s_%.*s_%u_%s",
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                pFileName->ParentDirectory.SequenceNumber,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                pFileName->ParentDirectory.SegmentNumberHighPart,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                pFileName->ParentDirectory.SegmentNumberLowPart,
                pFileName->FileNameLength,
                pFileName->FileName,
                (UINT)DataName.size(),
                DataName.c_str(),
                idx,
                pContent);
        else
            swprintf_s(
                tmpName,
                MAX_PATH,
                L"%*.*X%*.*X%*.*X__%.*s_%u_%s",
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                pFileName->ParentDirectory.SequenceNumber,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                pFileName->ParentDirectory.SegmentNumberHighPart,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                pFileName->ParentDirectory.SegmentNumberLowPart,
                pFileName->FileNameLength,
                pFileName->FileName,
                idx,
                pContent);
    }
    else
    {
        if (DataName.size())
            swprintf_s(
                tmpName,
                MAX_PATH,
                L"%*.*X%*.*X%*.*X__%.*s_%.*s_%s",
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                pFileName->ParentDirectory.SequenceNumber,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                pFileName->ParentDirectory.SegmentNumberHighPart,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                pFileName->ParentDirectory.SegmentNumberLowPart,
                pFileName->FileNameLength,
                pFileName->FileName,
                (UINT)DataName.size(),
                DataName.c_str(),
                pContent);
        else
        {
            swprintf_s(
                tmpName,
                MAX_PATH,
                L"%*.*X%*.*X%*.*X_%.*s_%s",
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                (int)sizeof(pFileName->ParentDirectory.SequenceNumber) * 2,
                pFileName->ParentDirectory.SequenceNumber,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberHighPart) * 2,
                pFileName->ParentDirectory.SegmentNumberHighPart,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                (int)sizeof(pFileName->ParentDirectory.SegmentNumberLowPart) * 2,
                pFileName->ParentDirectory.SegmentNumberLowPart,
                pFileName->FileNameLength,
                pFileName->FileName,
                pContent);
        }
    }

    SampleFileName.assign(tmpName);

    std::for_each(SampleFileName.begin(), SampleFileName.end(), [=](WCHAR& Item) {
        if (iswspace(Item) || Item == L':' || Item == L'#')
            Item = L'_';
    });

    return S_OK;
}

HRESULT Main::ConfigureSampleStreams(SampleRef& sampleRef)
{
    HRESULT hr = E_FAIL;

    shared_ptr<ByteStream> retval;

    _ASSERT(sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream->IsOpen() == S_OK);

    if (sampleRef.SampleName.empty())
        return E_INVALIDARG;

    std::shared_ptr<ByteStream> stream;

    switch (sampleRef.Content.Type)
    {
        case DATA:
            stream = sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream;
            break;
        case STRINGS:
        {
            auto strings = std::make_shared<StringsStream>(_L_);
            if (sampleRef.Content.MaxChars == 0 && sampleRef.Content.MinChars == 0)
            {
                if (FAILED(
                        hr = strings->OpenForStrings(
                            sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream,
                            config.content.MinChars,
                            config.content.MaxChars)))
                {
                    log::Error(_L_, hr, L"Failed to initialise strings stream\r\n");
                    return hr;
                }
            }
            else
            {
                if (FAILED(
                        hr = strings->OpenForStrings(
                            sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream,
                            sampleRef.Content.MinChars,
                            sampleRef.Content.MaxChars)))
                {
                    log::Error(_L_, hr, L"Failed to initialise strings stream\r\n");
                    return hr;
                }
            }
            stream = strings;
        }
        break;
        case RAW:
            stream = sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].RawStream;
            break;
        default:
            stream = sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream;
            break;
    }

    if (config.Output.XOR != 0L)
    {
        WCHAR szPrefixedName[MAX_PATH];

        shared_ptr<XORStream> pXORStream = make_shared<XORStream>(_L_);

        if (FAILED(hr = pXORStream->SetXORPattern(config.Output.XOR)))
        {
            return hr;
        }

        if (FAILED(hr = pXORStream->XORPrefixFileName(sampleRef.SampleName.c_str(), szPrefixedName, MAX_PATH)))
        {
            return hr;
        }

        sampleRef.SampleName.assign(szPrefixedName);

        sampleRef.HashStream = make_shared<CryptoHashStream>(_L_);

        CryptoHashStream::Algorithm algs = config.CryptoHashAlgs;

        if (FAILED(hr = sampleRef.HashStream->OpenToRead(algs, stream)))
            return hr;
        if (FAILED(hr = pXORStream->OpenForXOR(sampleRef.HashStream)))
            return hr;

        sampleRef.CopyStream = pXORStream;
    }
    else
    {
        std::shared_ptr<ByteStream> upstream = stream;

        CryptoHashStream::Algorithm algs = config.CryptoHashAlgs;

        if (algs != CryptoHashStream::Algorithm::Undefined)
        {
            sampleRef.HashStream = make_shared<CryptoHashStream>(_L_);
            if (FAILED(hr = sampleRef.HashStream->OpenToRead(algs, upstream)))
                return hr;
            upstream = sampleRef.HashStream;
        }

        FuzzyHashStream::Algorithm fuzzy_algs = config.FuzzyHashAlgs;
        if (fuzzy_algs != FuzzyHashStream::Algorithm::Undefined)
        {
            sampleRef.FuzzyHashStream = make_shared<FuzzyHashStream>(_L_);
            if (FAILED(hr = sampleRef.FuzzyHashStream->OpenToRead(fuzzy_algs, upstream)))
                return hr;
            upstream = sampleRef.FuzzyHashStream;
        }

        sampleRef.CopyStream = upstream;
    }

    sampleRef.SampleSize = sampleRef.CopyStream->GetSize();
    return S_OK;
}

LimitStatus Main::SampleLimitStatus(const Limits& GlobalLimits, const Limits& LocalLimits, DWORDLONG DataSize)
{
    if (GlobalLimits.bIgnoreLimits)
        return NoLimits;

    // Sample count reached?

    if (GlobalLimits.dwMaxSampleCount != INFINITE)
        if (GlobalLimits.dwAccumulatedSampleCount >= GlobalLimits.dwMaxSampleCount)
            return GlobalSampleCountLimitReached;

    if (LocalLimits.dwMaxSampleCount != INFINITE)
        if (LocalLimits.dwAccumulatedSampleCount >= LocalLimits.dwMaxSampleCount)
            return LocalSampleCountLimitReached;

    //  Global limits
    if (GlobalLimits.dwlMaxBytesPerSample != INFINITE)
        if (DataSize > GlobalLimits.dwlMaxBytesPerSample)
            return GlobalMaxBytesPerSample;

    if (GlobalLimits.dwlMaxBytesTotal != INFINITE)
        if (DataSize + GlobalLimits.dwlAccumulatedBytesTotal > GlobalLimits.dwlMaxBytesTotal)
            return GlobalMaxBytesTotal;

    // Local limits  bytes are now collected?
    if (LocalLimits.dwlMaxBytesPerSample != INFINITE)
        if (DataSize > LocalLimits.dwlMaxBytesPerSample)
            return LocalMaxBytesPerSample;
    if (LocalLimits.dwlMaxBytesTotal != INFINITE)
        if (DataSize + LocalLimits.dwlAccumulatedBytesTotal > LocalLimits.dwlMaxBytesTotal)
            return LocalMaxBytesTotal;

    return SampleWithinLimits;
}

HRESULT
Main::AddSampleRefToCSV(ITableOutput& output, const std::wstring& strComputerName, const Main::SampleRef& sampleRef)
{
    HRESULT hr = E_FAIL;

    static const FlagsDefinition AttrTypeDefs[] = {
        {$UNUSED, L"$UNUSED", L"$UNUSED"},
        {$STANDARD_INFORMATION, L"$STANDARD_INFORMATION", L"$STANDARD_INFORMATION"},
        {$ATTRIBUTE_LIST, L"$ATTRIBUTE_LIST", L"$ATTRIBUTE_LIST"},
        {$FILE_NAME, L"$FILE_NAME", L"$FILE_NAME"},
        {$OBJECT_ID, L"$OBJECT_ID", L"$OBJECT_ID"},
        {$SECURITY_DESCRIPTOR, L"$SECURITY_DESCRIPTOR", L"$SECURITY_DESCRIPTOR"},
        {$VOLUME_NAME, L"$VOLUME_NAME", L"$VOLUME_NAME"},
        {$VOLUME_INFORMATION, L"$VOLUME_INFORMATION", L"$VOLUME_INFORMATION"},
        {$DATA, L"$DATA", L"$DATA"},
        {$INDEX_ROOT, L"$INDEX_ROOT", L"$INDEX_ROOT"},
        {$INDEX_ALLOCATION, L"$INDEX_ALLOCATION", L"$INDEX_ALLOCATION"},
        {$BITMAP, L"$BITMAP", L"$BITMAP"},
        {$REPARSE_POINT, L"$REPARSE_POINT", L"$REPARSE_POINT"},
        {$EA_INFORMATION, L"$EA_INFORMATION", L"$EA_INFORMATION"},
        {$EA, L"$EA", L"$EA"},
        {$LOGGED_UTILITY_STREAM, L"$LOGGED_UTILITY_STREAM", L"$LOGGED_UTILITY_STREAM"},
        {$FIRST_USER_DEFINED_ATTRIBUTE, L"$FIRST_USER_DEFINED_ATTRIBUTE", L"$FIRST_USER_DEFINED_ATTRIBUTE"},
        {$END, L"$END", L"$END"}};

    for (auto match_it = begin(sampleRef.Matches); match_it != end(sampleRef.Matches); ++match_it)
    {
        for (auto name_it = begin((*match_it)->MatchingNames); name_it != end((*match_it)->MatchingNames); ++name_it)
        {
            output.WriteString(strComputerName.c_str());

            output.WriteInteger((*match_it)->VolumeReader->VolumeSerialNumber());

            {
                LARGE_INTEGER* pLI = (LARGE_INTEGER*)&(name_it->FILENAME()->ParentDirectory);
                output.WriteInteger((DWORDLONG)pLI->QuadPart);
            }
            {
                LARGE_INTEGER* pLI = (LARGE_INTEGER*)&((*match_it)->FRN);
                output.WriteInteger((DWORDLONG)pLI->QuadPart);
            }

            output.WriteString(name_it->FullPathName.c_str());

            if (sampleRef.OffLimits)
                output.WriteNothing();
            else
                output.WriteString(sampleRef.SampleName.c_str());

            output.WriteFileSize(sampleRef.SampleSize);

            if (!sampleRef.MD5.empty())
                output.WriteBytes(sampleRef.MD5);
            else
                output.WriteNothing();

            if (!sampleRef.SHA1.empty())
                output.WriteBytes(sampleRef.SHA1);
            else
                output.WriteNothing();

            output.WriteString((*match_it)->Term->GetDescription().c_str());

            switch (sampleRef.Content.Type)
            {
                case DATA:
                    output.WriteString(L"data");
                    break;
                case STRINGS:
                    output.WriteString(L"strings");
                    break;
                default:
                    output.WriteNothing();
            }

            output.WriteFileTime(sampleRef.CollectionDate);

            output.WriteFileTime((*match_it)->StandardInformation->CreationTime);
            output.WriteFileTime((*match_it)->StandardInformation->LastModificationTime);
            output.WriteFileTime((*match_it)->StandardInformation->LastAccessTime);
            output.WriteFileTime((*match_it)->StandardInformation->LastChangeTime);

            output.WriteFileTime(name_it->FILENAME()->Info.CreationTime);
            output.WriteFileTime(name_it->FILENAME()->Info.LastModificationTime);
            output.WriteFileTime(name_it->FILENAME()->Info.LastAccessTime);
            output.WriteFileTime(name_it->FILENAME()->Info.LastChangeTime);

            output.WriteExactFlags((*match_it)->MatchingAttributes[sampleRef.AttributeIndex].Type, AttrTypeDefs);

            output.WriteString((*match_it)->MatchingAttributes[sampleRef.AttributeIndex].AttrName.c_str());

            output.WriteInteger((DWORD)sampleRef.InstanceID);

            output.WriteGUID(sampleRef.SnapshotID);

            if (!sampleRef.SHA256.empty())
                output.WriteBytes(sampleRef.SHA256);
            else
                output.WriteNothing();

            if (!sampleRef.SSDeep.empty())
                output.WriteString(sampleRef.SSDeep.GetP<CHAR>());
            else
                output.WriteNothing();

            if (!sampleRef.TLSH.empty())
                output.WriteString(sampleRef.TLSH.GetP<CHAR>());
            else
                output.WriteNothing();

            const auto& rules = (*match_it)->MatchingAttributes[sampleRef.AttributeIndex].YaraRules;
            if (rules.has_value())
            {
                stringstream aStream;
                const char* const delim = ", ";

                std::copy(begin(rules.value()), end(rules.value()), std::ostream_iterator<std::string>(aStream, delim));

                output.WriteString(aStream.str().c_str());
            }
            else
            {
                output.WriteNothing();
            }

            output.WriteEndOfLine();
        }
    }
    return S_OK;
}

HRESULT
Main::AddSamplesForMatch(LimitStatus status, const SampleSpec& aSpec, const std::shared_ptr<FileFind::Match>& aMatch)
{
    HRESULT hr = E_FAIL;
    size_t sIndex = 0;

    for (const auto& anAttr : aMatch->MatchingAttributes)
    {
        SampleRef sampleRef;
        sampleRef.Matches.push_back(aMatch);

        sampleRef.VolumeSerial = aMatch->VolumeReader->VolumeSerialNumber();

        auto pSnapshotReader = std::dynamic_pointer_cast<SnapshotVolumeReader>(aMatch->VolumeReader);

        if (pSnapshotReader)
        {
            sampleRef.SnapshotID = pSnapshotReader->GetSnapshotID();
        }
        else
        {
            sampleRef.SnapshotID = GUID_NULL;
        }

        sampleRef.FRN = aMatch->FRN;
        sampleRef.InstanceID = anAttr.InstanceID;
        sampleRef.AttributeIndex = sIndex++;

        switch (status)
        {
            case NoLimits:
            case SampleWithinLimits:
                sampleRef.OffLimits = false;
                break;
            case GlobalSampleCountLimitReached:
            case GlobalMaxBytesPerSample:
            case GlobalMaxBytesTotal:
            case LocalSampleCountLimitReached:
            case LocalMaxBytesPerSample:
            case LocalMaxBytesTotal:
            case FailedToComputeLimits:
                sampleRef.OffLimits = true;
                break;
        }

        std::set<SampleRef>::iterator prevSample = Samples.find(sampleRef);

        if (prevSample != end(Samples))
        {
            // this sample is already cabbed
            log::Verbose(
                _L_,
                L"Not adding duplicate sample %s to archive\r\n",
                aMatch->MatchingNames.front().FullPathName.c_str());
            SampleRef& item = const_cast<SampleRef&>(*prevSample);
            hr = S_FALSE;
        }
        else
        {
            for (auto& name : aMatch->MatchingNames)
            {
                log::Verbose(_L_, L"Adding sample %s to archive\r\n", name.FullPathName.c_str());

                sampleRef.Content = aSpec.Content;
                sampleRef.CollectionDate = CollectionDate;

                wstring CabSampleName;
                DWORD dwIdx = 0L;
                std::set<std::wstring>::iterator it;
                do
                {
                    if (FAILED(
                            hr = CreateSampleFileName(
                                sampleRef.Content, name.FILENAME(), anAttr.AttrName, dwIdx, CabSampleName)))
                        break;

                    if (!aSpec.Name.empty())
                    {
                        CabSampleName.insert(0, L"\\");
                        CabSampleName.insert(0, aSpec.Name);
                    }
                    it = std::find(begin(SampleNames), end(SampleNames), CabSampleName);
                    dwIdx++;

                } while (it != end(SampleNames));

                SampleNames.insert(CabSampleName);
                sampleRef.SampleName = CabSampleName;
            }

            if (FAILED(hr = ConfigureSampleStreams(sampleRef)))
            {
                log::Error(_L_, hr, L"Failed to configure sample reference for %s\r\n", sampleRef.SampleName.c_str());
            }
            Samples.insert(sampleRef);
        }
    }

    if (hr == S_FALSE)
        return hr;
    return S_OK;
}

HRESULT Main::CollectMatchingSamples(
    const std::shared_ptr<ArchiveCreate>& compressor,
    ITableOutput& output,
    std::set<SampleRef>& Samples)
{
    HRESULT hr = E_FAIL;

    std::for_each(begin(Samples), end(Samples), [this, compressor, &hr](const SampleRef& sampleRef) {
        if (!sampleRef.OffLimits)
        {
            wstring strName;
            sampleRef.Matches.front()->GetMatchFullName(
                sampleRef.Matches.front()->MatchingNames.front(),
                sampleRef.Matches.front()->MatchingAttributes.front(),
                strName);
            if (FAILED(hr = compressor->AddStream(sampleRef.SampleName.c_str(), strName.c_str(), sampleRef.CopyStream)))
            {
                log::Error(_L_, hr, L"Failed to add sample %s\r\n", sampleRef.SampleName.c_str());
            }
        }
    });

    log::Info(_L_, L"\r\nAdding matching samples to archive:\r\n");
    compressor->SetCallback(
        [this](const Archive::ArchiveItem& item) { log::Info(_L_, L"\t%s\r\n", item.Path.c_str()); });

    if (FAILED(hr = compressor->FlushQueue()))
    {
        log::Error(_L_, hr, L"Failed to flush queue to %s\r\n", config.Output.Path.c_str());
        return hr;
    }

    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);

    std::for_each(
        begin(Samples), end(Samples), [this, strComputerName, compressor, &output, &hr](const SampleRef& sampleRef) {
            if (sampleRef.HashStream)
            {
                sampleRef.HashStream->GetMD5(const_cast<CBinaryBuffer&>(sampleRef.MD5));
                sampleRef.HashStream->GetSHA1(const_cast<CBinaryBuffer&>(sampleRef.SHA1));
                sampleRef.HashStream->GetSHA256(const_cast<CBinaryBuffer&>(sampleRef.SHA256));
            }

            if (sampleRef.FuzzyHashStream)
            {
                sampleRef.FuzzyHashStream->GetSSDeep(const_cast<CBinaryBuffer&>(sampleRef.SSDeep));
                sampleRef.FuzzyHashStream->GetTLSH(const_cast<CBinaryBuffer&>(sampleRef.TLSH));
            }

            if (FAILED(hr = AddSampleRefToCSV(output, strComputerName, sampleRef)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to add sample %s metadata to csv\r\n",
                    sampleRef.Matches.front()->MatchingNames.front().FullPathName.c_str());
                return;
            }
        });

    return S_OK;
}

HRESULT
Main::CollectMatchingSamples(const std::wstring& outputdir, ITableOutput& output, std::set<SampleRef>& MatchingSamples)
{
    HRESULT hr = E_FAIL;

    if (MatchingSamples.empty())
        return S_OK;

    fs::path output_dir(outputdir);

    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);

    log::Info(_L_, L"\r\nCopying matching samples to %s\r\n", outputdir.c_str());

    for (const auto& sample_ref : MatchingSamples)
    {
        if (!sample_ref.OffLimits)
        {
            fs::path sampleFile = output_dir / fs::path(sample_ref.SampleName);

            FileStream outputStream(_L_);

            if (FAILED(hr = outputStream.WriteTo(sampleFile.wstring().c_str())))
            {
                log::Error(_L_, hr, L"Failed to create sample file %s\r\n", sampleFile.wstring().c_str());
                break;
            }

            ULONGLONG ullBytesWritten = 0LL;
            if (FAILED(hr = sample_ref.CopyStream->CopyTo(outputStream, &ullBytesWritten)))
            {
                log::Error(_L_, hr, L"Failed while writing to sample %s\r\n", sampleFile.string().c_str());
                break;
            }

            outputStream.Close();
            sample_ref.CopyStream->Close();

            log::Info(
                _L_, L"\t%s copied (%I64d bytes)\r\n", sample_ref.SampleName.c_str(), sample_ref.CopyStream->GetSize());
        }
    }

    for (const auto& sample_ref : MatchingSamples)
    {
        fs::path sampleFile = output_dir / fs::path(sample_ref.SampleName);

        if (sample_ref.HashStream)
        {
            sample_ref.HashStream->GetMD5(const_cast<CBinaryBuffer&>(sample_ref.MD5));
            sample_ref.HashStream->GetSHA1(const_cast<CBinaryBuffer&>(sample_ref.SHA1));
            sample_ref.HashStream->GetSHA256(const_cast<CBinaryBuffer&>(sample_ref.SHA256));
        }

        if (sample_ref.FuzzyHashStream)
        {
            sample_ref.FuzzyHashStream->GetSSDeep(const_cast<CBinaryBuffer&>(sample_ref.SSDeep));
            sample_ref.FuzzyHashStream->GetTLSH(const_cast<CBinaryBuffer&>(sample_ref.TLSH));
        }

        if (FAILED(hr = AddSampleRefToCSV(output, strComputerName, sample_ref)))
        {
            log::Error(_L_, hr, L"Failed to add sample %s metadata to csv\r\n", sampleFile.string().c_str());
            break;
        }
    }
    return S_OK;
}

HRESULT Main::CollectMatchingSamples(const OutputSpec& output, std::set<SampleRef>& MatchingSamples)
{
    HRESULT hr = E_FAIL;

    switch (output.Type)
    {
        case OutputSpec::Archive:
        {
            auto compressor = ArchiveCreate::MakeCreate(config.Output.ArchiveFormat, _L_, true);

            if(!config.Output.Compression.empty())
                compressor->SetCompressionLevel(config.Output.Compression);

            auto [hr, CSV] = CreateArchiveLogFileAndCSV(config.Output.Path, compressor);
            if (FAILED(hr))
                return hr;

            if (config.bReportAll)
                if (FAILED(hr = HashOffLimitSamples(CSV->GetTableOutput(), MatchingSamples)))
                    return hr;

            if (FAILED(hr = CollectMatchingSamples(compressor, CSV->GetTableOutput(), MatchingSamples)))
                return hr;

            CSV->Flush();

            if (auto pStreamWriter = std::dynamic_pointer_cast<TableOutput::IStreamWriter>(CSV);
                pStreamWriter && pStreamWriter->GetStream())
            {
                auto pCSVStream = pStreamWriter->GetStream();

                if (pCSVStream && pCSVStream->GetSize() > 0LL)
                {
                    if (FAILED(hr = pCSVStream->SetFilePointer(0, FILE_BEGIN, nullptr)))
                    {
                        log::Error(_L_, hr, L"Failed to rewind csv stream\r\n");
                    }
                    if (FAILED(hr = compressor->AddStream(L"GetThis.csv", L"GetThis.csv", pCSVStream)))
                    {
                        log::Error(_L_, hr, L"Failed to add GetThis.csv\r\n");
                    }
                }
            }

            auto pLogStream = _L_->GetByteStream();
            _L_->CloseLogToStream(false);

            if (pLogStream && pLogStream->GetSize() > 0LL)
            {
                if (FAILED(hr = pLogStream->SetFilePointer(0, FILE_BEGIN, nullptr)))
                {
                    log::Error(_L_, hr, L"Failed to rewind log stream\r\n");
                }
                if (FAILED(hr = compressor->AddStream(L"GetThis.log", L"GetThis.log", pLogStream)))
                {
                    log::Error(_L_, hr, L"Failed to add GetThis.log\r\n");
                }
            }

            if (FAILED(hr = compressor->Complete()))
            {
                log::Error(_L_, hr, L"Failed to complete %s\r\n", config.Output.Path.c_str());
                return hr;
            }
            CSV->Close();
        }
        break;
        case OutputSpec::Directory:
        {
            auto [hr, CSV] = CreateOutputDirLogFileAndCSV(config.Output.Path);
            if (FAILED(hr))
                return hr;

            if (config.bReportAll)
                if (FAILED(hr = HashOffLimitSamples(CSV->GetTableOutput(), MatchingSamples)))
                    return hr;

            if (FAILED(hr = CollectMatchingSamples(config.Output.Path, CSV->GetTableOutput(), MatchingSamples)))
                return hr;

            CSV->Close();
        }
        break;
        default:
            return E_NOTIMPL;
    }

    return S_OK;
}

HRESULT Main::HashOffLimitSamples(ITableOutput& output, std::set<SampleRef>& MatchingSamples)
{
    HRESULT hr = E_FAIL;

    auto devnull = std::make_shared<DevNullStream>(_L_);

    log::Info(_L_, L"\r\nComputing hash of off limit samples\r\n");

    for (std::set<SampleRef>::iterator it = begin(MatchingSamples); it != end(MatchingSamples); ++it)
    {
        if (it->OffLimits)
        {
            ULONGLONG ullBytesWritten = 0LL;
            if (FAILED(hr = it->CopyStream->CopyTo(devnull, &ullBytesWritten)))
            {
                log::Error(_L_, hr, L"Failed while computing hash of sample\r\n");
                break;
            }

            it->CopyStream->Close();

            it->HashStream->GetMD5(const_cast<CBinaryBuffer&>(it->MD5));
            it->HashStream->GetSHA1(const_cast<CBinaryBuffer&>(it->SHA1));
        }
    }
    return S_OK;
}

HRESULT Main::FindMatchingSamples()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = FileFinder.InitializeYara(config.Yara)))
    {
        log::Error(_L_, hr, L"Failed to initialize Yara scan\r\n");
    }

    if (FAILED(
            hr = FileFinder.Find(
                config.Locations,
                [this, &hr](const std::shared_ptr<FileFind::Match>& aMatch, bool& bStop) {
                    if (aMatch == nullptr)
                        return;

                    // finding the corresponding Sample Spec (for limits)
                    auto aSpecIt = std::find_if(
                        begin(config.listofSpecs), end(config.listofSpecs), [aMatch](const SampleSpec& aSpec) -> bool {
                            auto filespecIt = std::find(begin(aSpec.Terms), end(aSpec.Terms), aMatch->Term);
                            return filespecIt != end(aSpec.Terms);
                        });

                    if (aSpecIt == end(config.listofSpecs))
                    {
                        log::Error(
                            _L_,
                            hr = E_FAIL,
                            L"Could not find sample spec for match %s\r\n",
                            aMatch->Term->GetDescription().c_str());
                        return;
                    }

                    const wstring& strFullFileName = aMatch->MatchingNames.front().FullPathName;

                    if (aMatch->MatchingAttributes.empty())
                    {
                        log::Warning(_L_, E_FAIL, L"\"%s\" matched \"%s\" but no data related attribute was associated\r\n", strFullFileName.c_str(), aMatch->Term->GetDescription().c_str());
                        return;
                    }

                    for (const auto& attr : aMatch->MatchingAttributes)
                    {
                        wstring strName;

                        aMatch->GetMatchFullName(aMatch->MatchingNames.front(), attr, strName);

                        DWORDLONG dwlDataSize = attr.DataStream->GetSize();
                        LimitStatus status = SampleLimitStatus(GlobalLimits, aSpecIt->PerSampleLimits, dwlDataSize);

                        if (FAILED(hr = AddSamplesForMatch(status, *aSpecIt, aMatch)))
                        {
                            log::Error(_L_, hr, L"\tFailed to add %s\r\n", strName.c_str());
                        }

                        switch (status)
                        {
                            case NoLimits:
                            case SampleWithinLimits:
                            {
                                if (hr == S_FALSE)
                                {
                                    log::Info(_L_, L"\t%s is already collected\r\n", strName.c_str());
                                }
                                else
                                {
                                    log::Info(_L_, L"\t%s matched (%d bytes)\r\n", strName.c_str(), dwlDataSize);
                                    aSpecIt->PerSampleLimits.dwlAccumulatedBytesTotal += dwlDataSize;
                                    aSpecIt->PerSampleLimits.dwAccumulatedSampleCount++;

                                    GlobalLimits.dwlAccumulatedBytesTotal += dwlDataSize;
                                    GlobalLimits.dwAccumulatedSampleCount++;
                                }
                            }
                            break;
                            case GlobalSampleCountLimitReached:
                                log::Info(
                                    _L_,
                                    L"\t%s : Global sample count reached (%d)\r\n",
                                    strName.c_str(),
                                    GlobalLimits.dwMaxSampleCount);
                                GlobalLimits.bMaxSampleCountReached = true;
                                break;
                            case GlobalMaxBytesPerSample:
                                log::Info(
                                    _L_,
                                    L"\t%s : Exceeds global per sample size limit (%I64d)\r\n",
                                    strName.c_str(),
                                    GlobalLimits.dwlMaxBytesPerSample);
                                GlobalLimits.bMaxBytesPerSampleReached = true;
                                break;
                            case GlobalMaxBytesTotal:
                                log::Info(
                                    _L_,
                                    L"\t%s : Global total sample size limit reached (%I64d)\r\n",
                                    strName.c_str(),
                                    GlobalLimits.dwlMaxBytesTotal);
                                GlobalLimits.bMaxBytesTotalReached = true;
                                break;
                            case LocalSampleCountLimitReached:
                                log::Info(
                                    _L_,
                                    L"\t%s : sample count reached (%d)\r\n",
                                    strName.c_str(),
                                    aSpecIt->PerSampleLimits.dwMaxSampleCount);
                                aSpecIt->PerSampleLimits.bMaxSampleCountReached = true;
                                break;
                            case LocalMaxBytesPerSample:
                                log::Info(
                                    _L_,
                                    L"\t%s : Exceeds per sample size limit (%I64d)\r\n",
                                    strName.c_str(),
                                    aSpecIt->PerSampleLimits.dwlMaxBytesPerSample);
                                aSpecIt->PerSampleLimits.bMaxBytesPerSampleReached = true;
                                break;
                            case LocalMaxBytesTotal:
                                log::Info(
                                    _L_,
                                    L"\t%s : total sample size limit reached (%I64d)\r\n",
                                    strName.c_str(),
                                    aSpecIt->PerSampleLimits.dwlMaxBytesTotal);
                                aSpecIt->PerSampleLimits.bMaxBytesTotalReached = true;
                                break;
                            case FailedToComputeLimits:
                                break;
                        }
                    }
                    return;
                },
                false)))
    {
        log::Error(_L_, hr, L"Failed while parsing locations\r\n");
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;
    LoadWinTrust();

    GetSystemTimeAsFileTime(&CollectionDate);

    if (config.strExtractCab.empty())
    {
        try
        {
            if (config.bFlushRegistry)
            {
                if (FAILED(hr = RegFlushKeys()))
                    log::Info(_L_, L"Failed to flush keys (hr = 0x%lx)\r\n", hr);
            }
        }
        catch (...)
        {
            log::Error(
                _L_, E_FAIL, L"GetThis failed during output setup, parameter output, RegistryFlush, exiting\r\n");
            return E_FAIL;
        }

        try
        {
            if (FAILED(hr = FindMatchingSamples()))
            {
                log::Error(_L_, hr, L"\r\nGetThis failed while matching samples\r\n");
                return hr;
            }

            if (FAILED(hr = CollectMatchingSamples(config.Output, Samples)))
            {
                log::Error(_L_, hr, L"\r\nGetThis failed while collecting samples\r\n");
                return hr;
            }
        }
        catch (...)
        {
            log::Error(_L_, E_ABORT, L"\r\nGetThis failed during sample collection, terminating cabinet\r\n");
            wstring strLogFileName(_L_->FileName());
            _L_->CloseLogFile();

            log::Error(_L_, E_ABORT, L"\r\nGetThis failed during sample collection, terminating cabinet\r\n");

            return E_ABORT;
        }
    }
    else
    {
        try
        {
            log::Info(_L_, L"Decompressing %s to %s\r\n", config.strExtractCab.c_str(), config.Output.Path.c_str());

            auto extract = ArchiveExtract::MakeExtractor(Archive::GetArchiveFormat(config.strExtractCab), _L_, true);

            if (FAILED(hr = extract->Extract(config.strExtractCab.c_str(), config.Output.Path.c_str(), nullptr)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Decompressing %s to %s failed\r\n",
                    config.strExtractCab.c_str(),
                    config.Output.Path.c_str());
                return hr;
            }
            else
            {
                log::Info(
                    _L_,
                    L"Decompressing %s to %s completed successfully\r\n",
                    config.strExtractCab.c_str(),
                    config.Output.Path.c_str(),
                    hr);
            }
        }
        catch (...)
        {
            log::Info(_L_, L"\r\nGetThis failed during cab extraction\r\n");
            return E_FAIL;
        }
    }
    return S_OK;
}
