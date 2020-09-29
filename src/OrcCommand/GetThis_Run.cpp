//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

#include <string>
#include <memory>
#include <filesystem>
#include <sstream>

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
#include "WinApiHelper.h"

#include "NtfsDataStructures.h"

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Command::GetThis;

namespace {

enum class CompressorFlags : uint32_t
{
    kNone = 0,
    kComputeHash = 1
};

std::shared_ptr<ArchiveCreate>
CreateCompressor(const OutputSpec& outputSpec, CompressorFlags flags, HRESULT& hr, logger& _L_)
{
    const bool computeHash = (static_cast<uint32_t>(flags) & static_cast<uint32_t>(CompressorFlags::kComputeHash));

    auto compressor = ArchiveCreate::MakeCreate(outputSpec.ArchiveFormat, _L_, computeHash);
    if (compressor == nullptr)
    {
        hr = E_POINTER;
        log::Error(_L_, hr, L"Failed calling MakeCreate for archive '%s'\r\n", outputSpec.Path.c_str());
        return nullptr;
    }

    hr = compressor->InitArchive(outputSpec.Path);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to initialize archive '%s'\r\n", outputSpec.Path.c_str());
        return nullptr;
    }

    if (!outputSpec.Password.empty())
    {
        hr = compressor->SetPassword(outputSpec.Password);
        if (FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to set password for '%s'\r\n", outputSpec.Path.c_str());
            return nullptr;
        }
    }

    hr = compressor->SetCompressionLevel(outputSpec.Compression);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to set compression level for '%s'\r\n", outputSpec.Path.c_str());
        return nullptr;
    }

    compressor->SetCallback(
        [&_L_](const OrcArchive::ArchiveItem& item) { log::Info(_L_, L"\t%s\r\n", item.Path.c_str()); });

    return compressor;
}

std::shared_ptr<TableOutput::IStreamWriter> CreateCsvWriter(
    const std::filesystem::path& out,
    const Orc::TableOutput::Schema& schema,
    const OutputSpec::Encoding& encoding,
    HRESULT& hr)
{
    auto csvStream = std::make_shared<TemporaryStream>();

    hr = csvStream->Open(out.parent_path(), out.filename(), 5 * 1024 * 1024);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to create temp stream (code: {:#x})", hr);
        return nullptr;
    }

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = encoding;

    auto csvWriter = TableOutput::CSV::Writer::MakeNew(std::move(options));
    hr = csvWriter->WriteToStream(csvStream);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to initialize CSV stream (code: {:#x})", hr);
        return nullptr;
    }

    hr = csvWriter->SetSchema(schema);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to set CSV schema (code: {:#x})", hr);
        return nullptr;
    }

    return csvWriter;
}

std::shared_ptr<TemporaryStream> CreateLogStream(const std::filesystem::path& out, HRESULT& hr, logger& _L_)
{
    auto logWriter = std::make_shared<LogFileWriter>(0x1000);
    logWriter->SetConsoleLog(_L_->ConsoleLog());
    logWriter->SetDebugLog(_L_->DebugLog());
    logWriter->SetVerboseLog(_L_->VerboseLog());

    auto logStream = std::make_shared<TemporaryStream>(logWriter);

    hr = logStream->Open(out.parent_path(), out.filename(), 5 * 1024 * 1024);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to create temp stream\r\n");
        return nullptr;
    }

    hr = _L_->LogToStream(logStream);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to initialize temp logging\r\n");
        return nullptr;
    }

    return logStream;
}

std::wstring RetrieveComputerName(const std::wstring& defaultName, logger& _L_)
{
    std::wstring name;

    HRESULT hr = SystemDetails::GetOrcComputerName(name);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to retrieve computer name (code: {:#x})", hr);
        return L"[unknown]";
    }

    return name;
}

HRESULT CopyStream(Orc::ByteStream& src, const fs::path& outPath)
{
    HRESULT hr = E_FAIL;
    std::error_code ec;

    fs::create_directories(outPath.parent_path(), ec);
    if (ec)
    {
        hr = HRESULT_FROM_WIN32(ec.value());
        Log::Error(L"Failed to create sample directory (code: {:#x})", hr);
        return hr;
    }

    FileStream outputStream;
    hr = outputStream.WriteTo(outPath.c_str());
    if (FAILED(hr))
    {
        Log::Error(L"Failed to create sample '{}' (code: {:#x})", outPath);
        return hr;
    }

    ULONGLONG ullBytesWritten = 0LL;
    hr = src.CopyTo(outputStream, &ullBytesWritten);
    if (FAILED(hr))
    {
        Log::Error(L"Failed while writing sample '{}' (code: {:#x})", outPath, hr);
        return hr;
    }

    hr = outputStream.Close();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to close sample '{}' (code: {:#x})", outPath, hr);
        return hr;
    }

    hr = src.Close();
    if (FAILED(hr))
    {
        Log::Warn(L"Failed to close input steam for '{}' (code: {:#x})", outPath, hr);
    }

    return S_OK;
}

std::wstring
GetMatchFullName(const FileFind::Match::NameMatch& nameMatch, const FileFind::Match::AttributeMatch& attrMatch)
{
    if (attrMatch.AttrName.empty())
    {
        return nameMatch.FullPathName;
    }

    std::wstring name;
    name.reserve(nameMatch.FullPathName.length() + 1 + attrMatch.AttrName.length());
    name.assign(nameMatch.FullPathName);

    switch (attrMatch.Type)
    {
        case $DATA:
            name.append(L":");
            break;
        default:
            name.append(L"#");
            break;
    }

    name.append(attrMatch.AttrName);
    return name;
}

const wchar_t* ToString(ContentType contentType)
{
    switch (contentType)
    {
        case ContentType::DATA:
            return L"data";
        case ContentType::STRINGS:
            return L"strings";
        case ContentType::RAW:
            return L"raw";
        default:
            return L"";
    }
}

std::wstring
CreateSampleFileName(const ContentType contentType, const PFILE_NAME pFileName, const std::wstring& dataName, DWORD idx)
{
    std::array<wchar_t, MAX_PATH> name;
    int len = 0;

    if (idx)
    {
        if (dataName.size())
        {
            len = swprintf_s(
                name.data(),
                name.size(),
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
                (UINT)dataName.size(),
                dataName.c_str(),
                idx,
                Command::GetThis::ToString(contentType).c_str());
        }
        else
        {
            len = swprintf_s(
                name.data(),
                name.size(),
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
                Command::GetThis::ToString(contentType).c_str());
        }
    }
    else
    {
        if (dataName.size())
        {

            len = swprintf_s(
                name.data(),
                name.size(),
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
                (UINT)dataName.size(),
                dataName.c_str(),
                Command::GetThis::ToString(contentType).c_str());
        }
        else
        {
            len = swprintf_s(
                name.data(),
                name.size(),
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
                Command::GetThis::ToString(contentType).c_str());
        }
    }

    if (len == -1)
    {
        return L"filename_error";
    }

    std::wstring sampleFileName;
    std::transform(std::cbegin(name), std::cbegin(name) + len, std::back_inserter(sampleFileName), [](wchar_t letter) {
        if (iswspace(letter) || letter == L':' || letter == L'#')
        {
            return L'_';
        }

        return letter;
    });

    return sampleFileName;
}

LimitStatus SampleLimitStatus(const Limits& globalLimits, const Limits& localLimits, DWORDLONG dataSize)
{
    if (globalLimits.bIgnoreLimits)
    {
        return LimitStatus::NoLimits;
    }

    if (globalLimits.dwMaxSampleCount != INFINITE)
    {
        if (globalLimits.dwAccumulatedSampleCount >= globalLimits.dwMaxSampleCount)
        {
            return GlobalSampleCountLimitReached;
        }
    }

    if (localLimits.dwMaxSampleCount != INFINITE)
    {
        if (localLimits.dwAccumulatedSampleCount >= localLimits.dwMaxSampleCount)
        {
            return LocalSampleCountLimitReached;
        }
    }

    if (globalLimits.dwlMaxBytesPerSample != INFINITE)
    {
        if (dataSize > globalLimits.dwlMaxBytesPerSample)
        {
            return GlobalMaxBytesPerSample;
        }
    }

    if (globalLimits.dwlMaxBytesTotal != INFINITE)
    {
        if (dataSize + globalLimits.dwlAccumulatedBytesTotal > globalLimits.dwlMaxBytesTotal)
        {
            return GlobalMaxBytesTotal;
        }
    }

    if (localLimits.dwlMaxBytesPerSample != INFINITE)
    {
        if (dataSize > localLimits.dwlMaxBytesPerSample)
        {
            return LocalMaxBytesPerSample;
        }
    }

    if (localLimits.dwlMaxBytesTotal != INFINITE)
    {
        if (dataSize + localLimits.dwlAccumulatedBytesTotal > localLimits.dwlMaxBytesTotal)
        {
            return LocalMaxBytesTotal;
        }
    }

    return SampleWithinLimits;
}

std::unique_ptr<ByteStream> ConfigureStringStream(
    const std::shared_ptr<ByteStream> dataStream,
    const ContentSpec& sampleSpec,
    const ContentSpec& configSpec)
{
    size_t minChars, maxChars;
    if (sampleSpec.MaxChars != 0 || sampleSpec.MinChars != 0)
    {
        minChars = sampleSpec.MinChars;
        maxChars = sampleSpec.MaxChars;
    }
    else
    {
        maxChars = configSpec.MaxChars;
        minChars = configSpec.MinChars;
    }

    auto stream = std::make_unique<StringsStream>();
    HRESULT hr = stream->OpenForStrings(dataStream, minChars, maxChars);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to initialise strings stream");
        return {};
    }

    return stream;
}

std::wstring CreateUniqueSampleName(
    const ContentType& contentType,
    const std::wstring& qualifier,
    const PFILE_NAME pFileName,
    const std::wstring& dataName,
    const std::unordered_set<std::wstring>& givenNames)
{

    _ASSERT(pFileName);
    if (pFileName == nullptr)
    {
        return {};
    }

    DWORD dwIdx = 0L;
    std::wstring name;
    std::unordered_set<std::wstring>::iterator it;
    do
    {
        name = ::CreateSampleFileName(contentType, pFileName, dataName, dwIdx);
        if (!qualifier.empty())
        {
            name.insert(0, L"\\");
            name.insert(0, qualifier);
        }

        it = givenNames.find(name);
        dwIdx++;

    } while (it != std::cend(givenNames));

    return name;
}

HRESULT RegFlushKeys()
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

class VolumeReaderInfo : public Orc::VolumeReaderVisitor
{
public:
    VolumeReaderInfo()
        : m_snapshotId(GUID_NULL)
    {
    }

    void Visit(const SnapshotVolumeReader& element) override { m_snapshotId = element.GetSnapshotID(); }

    const GUID& Guid() const { return m_snapshotId; }

private:
    GUID m_snapshotId;
};

}  // namespace

Main::Main(logger pLog)
    : UtilitiesMain(pLog)
    , config(pLog)
    , FileFinder(pLog)
    , ComputerName(::RetrieveComputerName(L"Default", pLog))
{
}

std::unique_ptr<Main::SampleRef> Main::CreateSample(
    const std::shared_ptr<FileFind::Match>& match,
    const size_t attributeIndex,
    const SampleSpec& sampleSpec,
    const std::unordered_set<std::wstring>& givenSampleNames) const
{
    const auto& attribute = match->MatchingAttributes[attributeIndex];

    auto sample = std::make_unique<Main::SampleRef>();

    sample->FRN = match->FRN;
    sample->VolumeSerial = match->VolumeReader->VolumeSerialNumber();
    sample->Content = sampleSpec.Content;
    sample->CollectionDate = CollectionDate;

    VolumeReaderInfo volumeInfo;
    match->VolumeReader->Accept(volumeInfo);
    sample->SnapshotID = volumeInfo.Guid();

    sample->Matches.push_back(match);

    sample->LimitStatus =
        ::SampleLimitStatus(GlobalLimits, sampleSpec.PerSampleLimits, attribute.DataStream->GetSize());

    sample->AttributeIndex = attributeIndex;
    sample->InstanceID = attribute.InstanceID;
    sample->SampleName = ::CreateUniqueSampleName(
        sample->Content.Type,
        sampleSpec.Name,
        match->MatchingNames[0].FILENAME(),
        attribute.AttrName,
        givenSampleNames);

    sample->SourcePath = ::GetMatchFullName(match->MatchingNames.front(), attribute);

    HRESULT hr = ConfigureSampleStreams(*sample);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to configure sample reference for '{}' (code: {:#x})", sample->SampleName, hr);
    }

    return sample;
}

HRESULT Main::ConfigureSampleStreams(SampleRef& sample) const
{
    HRESULT hr = E_FAIL;

    _ASSERT(sample.Matches.front()->MatchingAttributes[sample.AttributeIndex].DataStream->IsOpen() == S_OK);

    auto& dataStream = sample.Matches.front()->MatchingAttributes[sample.AttributeIndex].DataStream;

    // Stream are initially at eof

    std::shared_ptr<ByteStream> stream;
    if (sample.Content.Type == ContentType::STRINGS)
    {
        stream = ::ConfigureStringStream(dataStream, sample.Content, config.content);
        if (stream == nullptr)
        {
            return E_FAIL;
        }
    }
    else
    {
        stream = dataStream;
    }

    const auto algs = config.CryptoHashAlgs;
    if (algs != CryptoHashStream::Algorithm::Undefined)
    {
        sample.HashStream = std::make_shared<CryptoHashStream>();
        hr = sample.HashStream->OpenToRead(algs, stream);
        if (FAILED(hr))
        {
            return hr;
        }

        stream = sample.HashStream;
    }

    const auto fuzzyAlgs = config.FuzzyHashAlgs;
    if (fuzzyAlgs != FuzzyHashStream::Algorithm::Undefined)
    {
        sample.FuzzyHashStream = std::make_shared<FuzzyHashStream>();
        hr = sample.FuzzyHashStream->OpenToRead(fuzzyAlgs, stream);
        if (FAILED(hr))
        {
            return hr;
        }

        stream = sample.FuzzyHashStream;
    }

    sample.CopyStream = stream;
    sample.SampleSize = sample.CopyStream->GetSize();
    return S_OK;
}

HRESULT
Main::AddSampleRefToCSV(ITableOutput& output, const Main::SampleRef& sample) const
{
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

    for (const auto& match : sample.Matches)
    {
        for (const auto& name : match->MatchingNames)
        {
            output.WriteString(ComputerName);

            output.WriteInteger(match->VolumeReader->VolumeSerialNumber());

            {
                LARGE_INTEGER* pLI = (LARGE_INTEGER*)&(name.FILENAME()->ParentDirectory);
                output.WriteInteger((DWORDLONG)pLI->QuadPart);
            }
            {
                LARGE_INTEGER* pLI = (LARGE_INTEGER*)&(match->FRN);
                output.WriteInteger((DWORDLONG)pLI->QuadPart);
            }

            output.WriteString(name.FullPathName);

            if (sample.IsOfflimits())
            {
                output.WriteNothing();
            }
            else
            {
                output.WriteString(sample.SampleName);
            }

            output.WriteFileSize(sample.SampleSize);

            output.WriteBytes(sample.MD5);

            output.WriteBytes(sample.SHA1);

            output.WriteString(match->Term->GetDescription());

            switch (sample.Content.Type)
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

            output.WriteFileTime(sample.CollectionDate);

            output.WriteFileTime(match->StandardInformation->CreationTime);
            output.WriteFileTime(match->StandardInformation->LastModificationTime);
            output.WriteFileTime(match->StandardInformation->LastAccessTime);
            output.WriteFileTime(match->StandardInformation->LastChangeTime);

            output.WriteFileTime(name.FILENAME()->Info.CreationTime);
            output.WriteFileTime(name.FILENAME()->Info.LastModificationTime);
            output.WriteFileTime(name.FILENAME()->Info.LastAccessTime);
            output.WriteFileTime(name.FILENAME()->Info.LastChangeTime);

            output.WriteExactFlags(match->MatchingAttributes[sample.AttributeIndex].Type, AttrTypeDefs);

            output.WriteString(match->MatchingAttributes[sample.AttributeIndex].AttrName);

            output.WriteInteger((DWORD)sample.InstanceID);

            output.WriteGUID(sample.SnapshotID);

            output.WriteBytes(sample.SHA256);

            output.WriteBytes(sample.SSDeep);

            output.WriteBytes(sample.TLSH);

            const auto& rules = match->MatchingAttributes[sample.AttributeIndex].YaraRules;
            if (rules.has_value())
            {
                std::stringstream aStream;
                const char* const delim = "; ";

                std::copy(
                    std::cbegin(rules.value()),
                    std::cend(rules.value()),
                    std::ostream_iterator<std::string>(aStream, delim));

                output.WriteString(aStream.str());
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
Main::WriteSample(ArchiveCreate& compressor, std::unique_ptr<SampleRef> pSample, SampleWrittenCb writtenCb) const
{
    auto sample = std::shared_ptr<SampleRef>(std::move(pSample));

    const auto onItemArchivedCb = [this, sample, writtenCb](HRESULT hrArchived) {
        FinalizeHashes(*sample);

        HRESULT hrTable = AddSampleRefToCSV(*m_tableWriter, *sample);
        if (FAILED(hrTable))
        {
            log::Error(
                _L_,
                hrTable,
                L"Failed to add sample %s metadata to csv\r\n",
                sample->Matches.front()->MatchingNames.front().FullPathName.c_str());
        }

        if (writtenCb)
        {
            HRESULT hr = E_FAIL;
            if (SUCCEEDED(hrTable) && SUCCEEDED(hrArchived))
            {
                hr = S_OK;
            }

            writtenCb(*sample, hr);
        }
    };

    if (sample->IsOfflimits())
    {
        onItemArchivedCb(S_OK);
        return S_OK;
    }

    HRESULT hr = compressor.AddStream(
        sample->SampleName.c_str(), sample->SourcePath.c_str(), sample->CopyStream, onItemArchivedCb);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to add to archive the sample '%s'\r\n", sample->SampleName.c_str());
        // No need to call 'onItemArchivedCb' as it is 'AddStream' responsability
    }

    return hr;
}

HRESULT Main::WriteSample(
    const std::filesystem::path& outputDir,
    std::unique_ptr<SampleRef> sample,
    SampleWrittenCb writtenCb) const
{
    HRESULT hr = E_FAIL, hrCopy = E_FAIL, hrCsv = E_FAIL;

    if (!sample->IsOfflimits())
    {
        const fs::path sampleFile = outputDir / fs::path(sample->SampleName);
        hrCopy = ::CopyStream(*sample->CopyStream, sampleFile);
        if (FAILED(hrCopy))
        {
            Log::Error(L"Failed to copy stream of '{}' (code: {:#x})", sampleFile, hrCopy);
        }
    }

    FinalizeHashes(*sample);

    hrCsv = AddSampleRefToCSV(*m_tableWriter, *sample);
    if (FAILED(hrCsv))
    {
        Log::Error(
            L"Failed to add sample '{}' metadata to csv (code: {:#x})",
            sample->Matches.front()->MatchingNames.front().FullPathName,
            hrCsv);
    }

    if (SUCCEEDED(hrCopy) && SUCCEEDED(hrCsv))
    {
        hr = S_OK;
    }

    if (writtenCb)
    {
        writtenCb(*sample, hr);
    }

    return hr;
}

void Main::FinalizeHashes(const Main::SampleRef& sample) const
{
    if (!sample.HashStream)
    {
        return;
    }

    if (sample.IsOfflimits() && config.bReportAll && config.CryptoHashAlgs != CryptoHashStream::Algorithm::Undefined)
    {
        // Stream that were not collected must be read for HashStream
        ULONGLONG ullBytesWritten = 0LL;

        auto nullstream = DevNullStream();
        HRESULT hr = sample.CopyStream->CopyTo(nullstream, &ullBytesWritten);
        if (FAILED(hr))
        {
            Log::Error(L"Failed while computing hash of '{}' (code: {:#x})", sample.SampleName, hr);
        }

        sample.CopyStream->Close();
    }

    sample.HashStream->GetMD5(const_cast<CBinaryBuffer&>(sample.MD5));
    sample.HashStream->GetSHA1(const_cast<CBinaryBuffer&>(sample.SHA1));
    sample.HashStream->GetSHA256(const_cast<CBinaryBuffer&>(sample.SHA256));

    if (sample.FuzzyHashStream)
    {
        sample.FuzzyHashStream->GetSSDeep(const_cast<CBinaryBuffer&>(sample.SSDeep));
        sample.FuzzyHashStream->GetTLSH(const_cast<CBinaryBuffer&>(sample.TLSH));
    }
}

HRESULT Main::InitArchiveOutput()
{
    HRESULT hr = E_FAIL;

    const fs::path archivePath = config.Output.Path;
    auto tempDir = archivePath.parent_path();
    if (tempDir.empty())
    {
        std::error_code ec;
        tempDir = GetWorkingDirectoryApi(ec);
        if (ec)
        {
            Log::Warn(L"Failed to resolve current working directory (code: {:#x})", ec.value());
        }
    }

    ::CreateLogStream(tempDir / L"GetThisLogStream", hr, _L_);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to create log stream\r\n");
        return hr;
    }

    m_compressor = ::CreateCompressor(config.Output, CompressorFlags::kNone, hr, _L_);
    if (m_compressor == nullptr)
    {
        Log::Error(L"Failed to create compressor");
        return hr;
    }

    m_tableWriter =
        ::CreateCsvWriter(tempDir / L"GetThisCsvStream", config.Output.Schema, config.Output.OutputEncoding, hr);
    if (m_tableWriter == nullptr)
    {
        Log::Error(L"Failed to create csv stream (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

HRESULT Main::CloseArchiveOutput()
{
    _ASSERT(m_compressor);
    _ASSERT(m_tableWriter);

    m_compressor->FlushQueue();

    HRESULT hr = m_tableWriter->Flush();
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to flush csv writer\r\n");
    }

    auto tableStream = m_tableWriter->GetStream();
    if (tableStream && tableStream->GetSize())
    {
        hr = tableStream->SetFilePointer(0, FILE_BEGIN, nullptr);
        if (FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to rewind csv stream\r\n");
        }

        hr = m_compressor->AddStream(L"GetThis.csv", L"GetThis.csv", tableStream);
        if (FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to add GetThis.csv\r\n");
        }
    }

    auto logStream = _L_->GetByteStream();
    _L_->CloseLogToStream(false);

    if (logStream && logStream->GetSize() > 0LL)
    {
        hr = logStream->SetFilePointer(0, FILE_BEGIN, nullptr);
        if (FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to rewind log stream\r\n");
        }

        hr = m_compressor->AddStream(L"GetThis.log", L"GetThis.log", logStream);
        if (FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to add GetThis.log\r\n");
        }
    }

    hr = m_compressor->Complete();
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to complete %s\r\n", config.Output.Path.c_str());
        return hr;
    }

    hr = tableStream->Close();
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to close csv writer\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::InitDirectoryOutput()
{
    HRESULT hr = E_FAIL;
    std::error_code ec;

    const fs::path outputDir(config.Output.Path);
    fs::create_directories(outputDir, ec);
    if (ec)
    {
        hr = HRESULT_FROM_WIN32(ec.value());
        Log::Error(L"Failed to create output directory (code: {:#x})", hr);
        return hr;
    }

    m_tableWriter =
        ::CreateCsvWriter(outputDir / L"GetThis.csv", config.Output.Schema, config.Output.OutputEncoding, hr);
    if (m_tableWriter == nullptr)
    {
        Log::Error(L"Failed to create csv stream (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

HRESULT Main::CloseDirectoryOutput()
{
    HRESULT hr = m_tableWriter->Flush();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to flush table stream (code: {:#x})", hr);
        return hr;
    }

    hr = m_tableWriter->Close();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to close table stream (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

void Main::UpdateSamplesLimits(SampleSpec& sampleSpec, const SampleRef& sample)
{
    switch (sample.LimitStatus)
    {
        case NoLimits:
        case SampleWithinLimits: {
            sampleSpec.PerSampleLimits.dwlAccumulatedBytesTotal += sample.SampleSize;
            sampleSpec.PerSampleLimits.dwAccumulatedSampleCount++;
            GlobalLimits.dwlAccumulatedBytesTotal += sample.SampleSize;
            GlobalLimits.dwAccumulatedSampleCount++;
        }
        break;

        case GlobalSampleCountLimitReached:
            GlobalLimits.bMaxSampleCountReached = true;
            break;

        case GlobalMaxBytesPerSample:
            GlobalLimits.bMaxBytesPerSampleReached = true;
            break;

        case GlobalMaxBytesTotal:
            GlobalLimits.bMaxBytesTotalReached = true;
            break;

        case LocalSampleCountLimitReached:
            sampleSpec.PerSampleLimits.bMaxSampleCountReached = true;
            break;

        case LocalMaxBytesPerSample:
            sampleSpec.PerSampleLimits.bMaxBytesPerSampleReached = true;
            break;

        case LocalMaxBytesTotal:
            sampleSpec.PerSampleLimits.bMaxBytesTotalReached = true;
            break;

        case FailedToComputeLimits:
            break;

        default:
            _ASSERT("Unhandled 'LimitStatus' case");
    }
}

void Main::OnSampleWritten(const SampleRef& sample, const SampleSpec& sampleSpec, HRESULT hrWrite) const
{
    const auto& name = sample.SourcePath.c_str();

    if (FAILED(hrWrite))
    {
        Log::Error(L"[FAILED] '{}', {} bytes (code: {:#x})", name, sample.SampleSize, hrWrite);
        return;
    }

    switch (sample.LimitStatus)
    {
        case NoLimits:
        case SampleWithinLimits:
            m_console.Print(L"{} matched ({} bytes)", name, sample.SampleSize);
            break;

        case GlobalSampleCountLimitReached:
            m_console.Print(L"{}: Global sample count reached ({})", name, GlobalLimits.dwMaxSampleCount);
            break;

        case GlobalMaxBytesPerSample:
            m_console.Print(L"{}: Exceeds global per sample size limit ({})", name, GlobalLimits.dwlMaxBytesPerSample);
            break;

        case GlobalMaxBytesTotal:
            m_console.Print(L"{}: Global total sample size limit reached ({})", name, GlobalLimits.dwlMaxBytesTotal);
            break;

        case LocalSampleCountLimitReached:
            m_console.Print(L"{}: sample count reached ({})", name, sampleSpec.PerSampleLimits.dwMaxSampleCount);
            break;

        case LocalMaxBytesPerSample:
            m_console.Print(
                L"{}: Exceeds per sample size limit ({})", name, sampleSpec.PerSampleLimits.dwlMaxBytesPerSample);
            break;

        case LocalMaxBytesTotal:
            m_console.Print(
                L"{}: total sample size limit reached ({})", name, sampleSpec.PerSampleLimits.dwlMaxBytesTotal);
            break;

        case FailedToComputeLimits:
            break;
    }
}

void Main::OnMatchingSample(const std::shared_ptr<FileFind::Match>& aMatch, bool bStop)
{
    HRESULT hr = E_FAIL;

    _ASSERT(aMatch != nullptr);

    if (aMatch->MatchingAttributes.empty())
    {
        Log::Error(
            L"'{}' matched '{}' but no data related attribute was associated",
            aMatch->MatchingNames.front().FullPathName,
            aMatch->Term->GetDescription());
        return;
    }

    // finding the corresponding Sample Spec (for limits)
    auto sampleSpecIt = std::find_if(
        std::begin(config.listofSpecs), std::end(config.listofSpecs), [aMatch](const SampleSpec& aSpec) -> bool {
            auto filespecIt = std::find(std::begin(aSpec.Terms), std::end(aSpec.Terms), aMatch->Term);
            return filespecIt != std::end(aSpec.Terms);
        });

    if (sampleSpecIt == std::cend(config.listofSpecs))
    {
        Log::Error(L"Could not find sample spec for match '{}'", aMatch->Term->GetDescription());
        return;
    }

    auto& sampleSpec = *sampleSpecIt;

    for (size_t i = 0; i < aMatch->MatchingAttributes.size(); ++i)
    {
        const auto& attribute = aMatch->MatchingAttributes[i];

        auto sample = CreateSample(aMatch, i, sampleSpec, SampleNames);
        if (m_sampleIds.find(SampleId(*sample)) != std::cend(m_sampleIds))
        {
            Log::Debug(L"Not adding duplicate sample '{}' to archive", aMatch->MatchingNames.front().FullPathName);
            continue;
        }

        UpdateSamplesLimits(sampleSpec, *sample);

        // TODO: check that both sampleIds and SampleNames are resetted when volume changes
        SampleNames.insert(sample->SampleName);
        m_sampleIds.insert(SampleId(*sample));

        hr = WriteSample(*m_compressor, std::move(sample), [this, &sampleSpec](const SampleRef& sample, HRESULT hr) {
            OnSampleWritten(sample, sampleSpec, hr);
        });

        if (FAILED(hr))
        {
            Log::Warn(L"Failed to add sample");
            continue;
        }
    }
}

HRESULT Main::FindMatchingSamples()
{
    HRESULT hr = E_FAIL;

    hr = FileFinder.InitializeYara(config.Yara);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to initialize Yara scan");
    }

    hr = FileFinder.Find(
        config.Locations,
        std::bind(&Main::OnMatchingSample, this, std::placeholders::_1, std::placeholders::_2),
        false);

    if (FAILED(hr))
    {
        Log::Error(L"Failed while parsing locations");
    }

    return S_OK;
}

HRESULT Main::InitOutput()
{
    if (config.Output.Type == OutputSpec::Kind::Archive)
    {
        return InitArchiveOutput();
    }
    else if (config.Output.Type == OutputSpec::Kind::Directory)
    {
        return InitDirectoryOutput();
    }

    return E_NOTIMPL;
}

HRESULT Main::CloseOutput()
{
    HRESULT hr = E_FAIL;

    if (config.Output.Type == OutputSpec::Kind::Archive)
    {
        hr = CloseArchiveOutput();
        if (FAILED(hr))
        {
            Log::Error(L"Cannot close archive output (code: {:#x})", hr);
            return hr;
        }
    }
    else if (config.Output.Type == OutputSpec::Kind::Directory)
    {
        hr = CloseDirectoryOutput();
        if (FAILED(hr))
        {
            Log::Error(L"Cannot close directory output (code: {:#x})", hr);
            return hr;
        }
    }

    return hr;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    LoadWinTrust();

    GetSystemTimeAsFileTime(&CollectionDate);

    try
    {
        if (config.bFlushRegistry)
        {
            hr = ::RegFlushKeys();
            if (FAILED(hr))
            {
                Log::Error(L"Failed to flush keys (code: {:#x})", hr);
                return hr;
            }
        }

        hr = InitOutput();
        if (FAILED(hr))
        {
            Log::Error(L"Cannot initialize output mode (code: {:#x})", hr);
            return hr;
        }

        hr = FindMatchingSamples();
        if (FAILED(hr))
        {
            Log::Error(L"GetThis failed while matching samples (code: {:#x})", hr);
            return hr;
        }

        hr = CloseOutput();
        if (FAILED(hr))
        {
            Log::Error(L"Failed to close output (code: {:#x})", hr);
        }
    }
    catch (...)
    {
        Log::Error(L"GetThis failed during sample collection, terminating archive");
        return E_ABORT;
    }

    return S_OK;
}
