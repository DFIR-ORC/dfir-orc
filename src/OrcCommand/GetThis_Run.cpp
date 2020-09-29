//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

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
#include "CryptoHashStream.h"
#include "ParameterCheck.h"
#include "ArchiveExtract.h"

#include "SnapshotVolumeReader.h"

#include "SystemDetails.h"

#include <string>
#include <memory>
#include <filesystem>
#include <sstream>

#include "Log/Log.h"

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
                Log::Debug(L"Created output directory '{}'", strOutputDir);
            }
            else
            {
                Log::Debug(L"Output directory '{}' exists", strOutputDir);
            }
            break;
        case fs::file_type::directory:
            Log::Debug(L"Specified output directory '{}' exists and is a directory", strOutputDir);
            break;
        default:
            Log::Error(L"Specified output directory '{}' exists and is not a directory", strOutputDir);
            break;
    }

    fs::path csvFile(outDir);
    csvFile /= L"GetThis.csv";

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;
    auto CSV = TableOutput::CSV::Writer::MakeNew(std::move(options));

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

    auto csvStream = std::make_shared<TemporaryStream>();

    if (FAILED(hr = csvStream->Open(tempdir.wstring(), L"GetThisCsvStream", 1 * 1024 * 1024)))
    {
        Log::Error("Failed to create temp stream");
        return {hr, nullptr};
    }

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;

    auto CSV = TableOutput::CSV::Writer::MakeNew(std::move(options));
    if (FAILED(hr = CSV->WriteToStream(csvStream)))
    {
        Log::Error("Failed to initialize CSV stream");
        return {hr, nullptr};
    }

    CSV->SetSchema(config.Output.Schema);

    if (FAILED(hr = compressor->InitArchive(pArchivePath.c_str())))
    {
        Log::Error(L"Failed to initialize archive file {}", pArchivePath);
        return {hr, nullptr};
    }

    if (!config.Output.Password.empty())
    {
        if (FAILED(hr = compressor->SetPassword(config.Output.Password)))
        {
            Log::Error(L"Failed to set password for archive file {}", pArchivePath);
            return {hr, nullptr};
        }
    }

    return {S_OK, std::move(CSV)};
}

HRESULT Main::RegFlushKeys()
{
    bool bSuccess = true;
    DWORD dwGLE = 0L;

    Log::Debug(L"Flushing HKEY_LOCAL_MACHINE");
    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
    {
        bSuccess = false;
    }

    Log::Debug(L"Flushing HKEY_USERS");
    dwGLE = RegFlushKey(HKEY_USERS);
    if (dwGLE != ERROR_SUCCESS)
    {
        bSuccess = false;
    }

    if (!bSuccess)
    {
        return HRESULT_FROM_WIN32(dwGLE);
    }

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
    std::wstring contentType = Orc::Command::GetThis::ToString(content.Type);

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
                contentType.c_str());
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
                contentType.c_str());
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
                contentType.c_str());
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
                contentType.c_str());
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
        case ContentType::DATA:
            stream = sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream;
            break;
        case ContentType::STRINGS: {
            auto strings = std::make_shared<StringsStream>();
            if (sampleRef.Content.MaxChars == 0 && sampleRef.Content.MinChars == 0)
            {
                if (FAILED(
                        hr = strings->OpenForStrings(
                            sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream,
                            config.content.MinChars,
                            config.content.MaxChars)))
                {
                    Log::Error("Failed to initialise strings stream");
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
                    Log::Error("Failed to initialise strings stream");
                    return hr;
                }
            }
            stream = strings;
        }
        break;
        case ContentType::RAW:
            stream = sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].RawStream;
            break;
        default:
            stream = sampleRef.Matches.front()->MatchingAttributes[sampleRef.AttributeIndex].DataStream;
            break;
    }

    std::shared_ptr<ByteStream> upstream = stream;

    CryptoHashStream::Algorithm algs = config.CryptoHashAlgs;

    if (algs != CryptoHashStream::Algorithm::Undefined)
    {
        sampleRef.HashStream = make_shared<CryptoHashStream>();
        if (FAILED(hr = sampleRef.HashStream->OpenToRead(algs, upstream)))
            return hr;
        upstream = sampleRef.HashStream;
    }
    else
    {
        upstream = stream;
    }

    FuzzyHashStream::Algorithm fuzzy_algs = config.FuzzyHashAlgs;
    if (fuzzy_algs != FuzzyHashStream::Algorithm::Undefined)
    {
        sampleRef.FuzzyHashStream = make_shared<FuzzyHashStream>();
        if (FAILED(hr = sampleRef.FuzzyHashStream->OpenToRead(fuzzy_algs, upstream)))
            return hr;
        upstream = sampleRef.FuzzyHashStream;
    }

    sampleRef.CopyStream = upstream;
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
                case ContentType::DATA:
                    output.WriteString(L"data");
                    break;
                case ContentType::STRINGS:
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
                const char* const delim = "; ";

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

        SampleSet::iterator prevSample = Samples.find(sampleRef);

        if (prevSample != end(Samples))
        {
            // this sample is already cabbed
            Log::Debug(L"Not adding duplicate sample {} to archive", aMatch->MatchingNames.front().FullPathName);
            SampleRef& item = const_cast<SampleRef&>(*prevSample);
            hr = S_FALSE;
        }
        else
        {
            for (auto& name : aMatch->MatchingNames)
            {
                Log::Debug(L"Adding sample {} to archive", name.FullPathName);

                sampleRef.Content = aSpec.Content;
                sampleRef.CollectionDate = CollectionDate;

                wstring CabSampleName;
                DWORD dwIdx = 0L;
                std::unordered_set<std::wstring>::iterator it;
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
                    it = SampleNames.find(CabSampleName);
                    dwIdx++;

                } while (it != end(SampleNames));

                SampleNames.insert(CabSampleName);
                sampleRef.SampleName = CabSampleName;
            }

            if (FAILED(hr = ConfigureSampleStreams(sampleRef)))
            {
                Log::Error(L"Failed to configure sample reference for {}", sampleRef.SampleName);
            }

            Samples.insert(sampleRef);
        }
    }

    if (hr == S_FALSE)
        return hr;
    return S_OK;
}

HRESULT
Main::CollectMatchingSamples(const std::shared_ptr<ArchiveCreate>& compressor, ITableOutput& output, SampleSet& Samples)
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
                Log::Error(L"Failed to add sample {}", sampleRef.SampleName);
            }
        }
    });

    auto root = m_console.OutputTree();
    auto matchingSamplesNode = root.AddNode("Adding matching samples to archive:");
    compressor->SetCallback(
        [this, matchingSamplesNode](const Archive::ArchiveItem& item) mutable { matchingSamplesNode.Add(item.Path); });

    if (FAILED(hr = compressor->FlushQueue()))
    {
        Log::Error(L"Failed to flush queue to {}", config.Output.Path);
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
                Log::Error(
                    L"Failed to add sample '{}' metadata to csv (code: {})",
                    sampleRef.Matches.front()->MatchingNames.front().FullPathName,
                    hr);
                return;
            }
        });

    return S_OK;
}

HRESULT
Main::CollectMatchingSamples(const std::wstring& outputdir, ITableOutput& output, SampleSet& MatchingSamples)
{
    HRESULT hr = E_FAIL;

    if (MatchingSamples.empty())
        return S_OK;

    fs::path output_dir(outputdir);

    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);

    auto root = m_console.OutputTree();
    auto matchingSamplesNode = root.AddNode("Copying matching samples to '{}'", outputdir);

    for (const auto& sample_ref : MatchingSamples)
    {
        if (!sample_ref.OffLimits)
        {
            fs::path sampleFile = output_dir / fs::path(sample_ref.SampleName);

            FileStream outputStream;

            if (FAILED(hr = outputStream.WriteTo(sampleFile.wstring().c_str())))
            {
                Log::Error("Failed to create sample file '{}'", sampleFile.string());
                break;
            }

            ULONGLONG ullBytesWritten = 0LL;
            if (FAILED(hr = sample_ref.CopyStream->CopyTo(outputStream, &ullBytesWritten)))
            {
                Log::Error("Failed while writing to sample '{}'", sampleFile.string());
                break;
            }

            outputStream.Close();
            sample_ref.CopyStream->Close();

            matchingSamplesNode.Add("'{}' copied ({} bytes)", sample_ref.SampleName, sample_ref.CopyStream->GetSize());
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
            Log::Error("Failed to add sample '{}' metadata to csv", sampleFile.string());
            break;
        }
    }
    return S_OK;
}

HRESULT Main::CollectMatchingSamples(const OutputSpec& output, SampleSet& MatchingSamples)
{
    HRESULT hr = E_FAIL;

    switch (output.Type)
    {
        case OutputSpec::Kind::Archive: {
            auto compressor = ArchiveCreate::MakeCreate(config.Output.ArchiveFormat, false);

            if (!config.Output.Compression.empty())
                compressor->SetCompressionLevel(config.Output.Compression);

            auto [hr, CSV] = CreateArchiveLogFileAndCSV(config.Output.Path, compressor);
            if (FAILED(hr))
                return hr;

            if (config.bReportAll && config.CryptoHashAlgs != CryptoHashStream::Algorithm::Undefined)
                if (FAILED(hr = HashOffLimitSamples(*CSV, MatchingSamples)))
                    return hr;

            if (FAILED(hr = CollectMatchingSamples(compressor, *CSV, MatchingSamples)))
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
                        Log::Error("Failed to rewind csv stream");
                    }
                    if (FAILED(hr = compressor->AddStream(L"GetThis.csv", L"GetThis.csv", pCSVStream)))
                    {
                        Log::Error("Failed to add GetThis.csv");
                    }
                }
            }

            if (FAILED(hr = compressor->Complete()))
            {
                Log::Error(L"Failed to complete '{}'", config.Output.Path);
                return hr;
            }
            CSV->Close();
        }
        break;
        case OutputSpec::Kind::Directory: {
            auto [hr, CSV] = CreateOutputDirLogFileAndCSV(config.Output.Path);
            if (FAILED(hr))
                return hr;

            if (config.bReportAll && config.CryptoHashAlgs != CryptoHashStream::Algorithm::Undefined)
                if (FAILED(hr = HashOffLimitSamples(*CSV, MatchingSamples)))
                    return hr;

            if (FAILED(hr = CollectMatchingSamples(config.Output.Path, *CSV, MatchingSamples)))
                return hr;

            CSV->Close();
        }
        break;
        default:
            return E_NOTIMPL;
    }

    return S_OK;
}

HRESULT Main::HashOffLimitSamples(ITableOutput& output, SampleSet& MatchingSamples)
{
    HRESULT hr = E_FAIL;

    auto devnull = std::make_shared<DevNullStream>();

    m_console.Print("Computing hash of off limit samples");

    for (SampleSet::iterator it = begin(MatchingSamples); it != end(MatchingSamples); ++it)
    {
        if (it->OffLimits)
        {
            ULONGLONG ullBytesWritten = 0LL;
            if (FAILED(hr = it->CopyStream->CopyTo(devnull, &ullBytesWritten)))
            {
                Log::Error("Failed while computing hash of sample");
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
        Log::Error("Failed to initialize Yara scan");
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
                        Log::Error(L"Could not find sample spec for match '{}'", aMatch->Term->GetDescription());
                        return;
                    }

                    const wstring& strFullFileName = aMatch->MatchingNames.front().FullPathName;

                    if (aMatch->MatchingAttributes.empty())
                    {
                        Log::Warn(
                            L"'{}' matched '{}' but no data related attribute was associated",
                            strFullFileName,
                            aMatch->Term->GetDescription());
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
                            Log::Error(L"Failed to add {}", strName);
                        }

                        switch (status)
                        {
                            case NoLimits:
                            case SampleWithinLimits: {
                                if (hr == S_FALSE)
                                {
                                    m_console.Print(L"'{}' is already collected", strName);
                                }
                                else
                                {
                                    m_console.Print(L"'{}' matched ({} bytes)", strName, dwlDataSize);
                                    aSpecIt->PerSampleLimits.dwlAccumulatedBytesTotal += dwlDataSize;
                                    aSpecIt->PerSampleLimits.dwAccumulatedSampleCount++;

                                    GlobalLimits.dwlAccumulatedBytesTotal += dwlDataSize;
                                    GlobalLimits.dwAccumulatedSampleCount++;
                                }
                            }
                            break;
                            case GlobalSampleCountLimitReached:
                                m_console.Print(
                                    L"'{}': Global sample count reached ({})", strName, GlobalLimits.dwMaxSampleCount);
                                GlobalLimits.bMaxSampleCountReached = true;
                                break;
                            case GlobalMaxBytesPerSample:
                                m_console.Print(
                                    L"'{}': Exceeds global per sample size limit ({})",
                                    strName,
                                    GlobalLimits.dwlMaxBytesPerSample);
                                GlobalLimits.bMaxBytesPerSampleReached = true;
                                break;
                            case GlobalMaxBytesTotal:
                                m_console.Print(
                                    L"'{}': Global total sample size limit reached ({})",
                                    strName,
                                    GlobalLimits.dwlMaxBytesTotal);
                                GlobalLimits.bMaxBytesTotalReached = true;
                                break;
                            case LocalSampleCountLimitReached:
                                m_console.Print(
                                    L"'{}': sample count reached ({})",
                                    strName,
                                    aSpecIt->PerSampleLimits.dwMaxSampleCount);
                                aSpecIt->PerSampleLimits.bMaxSampleCountReached = true;
                                break;
                            case LocalMaxBytesPerSample:
                                m_console.Print(
                                    L"{}: Exceeds per sample size limit ({})",
                                    strName,
                                    aSpecIt->PerSampleLimits.dwlMaxBytesPerSample);
                                aSpecIt->PerSampleLimits.bMaxBytesPerSampleReached = true;
                                break;
                            case LocalMaxBytesTotal:
                                m_console.Print(
                                    L"{}: total sample size limit reached ({})",
                                    strName,
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
        Log::Error("Failed while parsing locations");
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;
    LoadWinTrust();

#ifndef _DEBUG
    GetSystemTimeAsFileTime(&CollectionDate);
#else
    CollectionDate = {0};
#endif

    try
    {
        if (config.bFlushRegistry)
        {
            if (FAILED(hr = RegFlushKeys()))
                Log::Warn("Failed to flush keys (code: {:#x})", hr);
        }
    }
    catch (...)
    {
        Log::Error(L"GetThis failed during output setup, parameter output, RegistryFlush, exiting");
        return E_FAIL;
    }

    try
    {
        if (FAILED(hr = FindMatchingSamples()))
        {
            Log::Error("GetThis failed while matching samples");
            return hr;
        }
        if (FAILED(hr = CollectMatchingSamples(config.Output, Samples)))
        {
            Log::Error("GetThis failed while collecting samples");
            return hr;
        }
    }
    catch (...)
    {
        Log::Error("GetThis failed during sample collection, terminating archive");
        return E_ABORT;
    }

    return S_OK;
}
