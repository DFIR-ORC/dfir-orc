//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

#include <string>
#include <memory>
#include <filesystem>
#include <sstream>

#include "fmt/chrono.h"
#include "TableOutput.h"
#include "CsvFileWriter.h"
#include "Configuration/ConfigFileReader.h"
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
#include "StructuredOutputWriter.h"
#include "SnapshotVolumeReader.h"

#include "SystemDetails.h"
#include "Utils/WinApi.h"
#include "Utils/TypeTraits.h"
#include "Utils/String.h"

#include "NtfsDataStructures.h"

#include "Archive/CompressionLevel.h"
#include "Archive/Appender.h"
#include "Archive/7z/Archive7z.h"
#include "Text/Fmt/Result.h"
#include "Text/Fmt/Limit.h"

namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Command::GetThis;

namespace {

const std::wstring_view kGetThisCsv = L"GetThis.csv";
const std::wstring_view kGetThisStatistics = L"Statistics.json";

enum class CompressorFlags : uint32_t
{
    kNone = 0,
    kComputeHash = 1
};

std::wstring ToString(const GUID& guid)
{
    std::wstring s;
    s.resize(MAX_GUID_STRLEN);

    auto guidCch = StringFromGUID2(guid, s.data(), s.size());
    if (guidCch == 0)
    {
        // Cannot only fail with a too small buffer but MAX_GUID_STRLEN is used...
        Log::Warn("Failed to convert GUID, buffer is too small");
        return {};
    }

    s.resize(guidCch);
    return s;
}

void PrintStatistics(Orc::Text::Tree& root, const std::vector<std::shared_ptr<FileFind::SearchTerm>>& searchTerms)
{
    auto statsNode = root.AddNode(L"Statistics for 'ntfs_find' rules:");
    statsNode.AddEmptyLine();

    if (searchTerms.empty())
    {
        statsNode.Add(L"<No rule defined>");
        statsNode.AddEmptyLine();
        return;
    }

    for (const auto& searchTerm : searchTerms)
    {
        const auto& stats = searchTerm->GetProfilingStatistics();

        auto& rule = searchTerm->GetRule();
        if (rule.empty())
        {
            Log::Warn("Failed to display statistics for an 'ntfs_find' empty rule");
            continue;
        }

        if (StartsWith(searchTerm->m_rule, L"<ntfs_exclude"))
        {
            auto ruleNode = statsNode.AddNode(searchTerm->GetRule());
            ruleNode.Add(
                "Match: {:%H:%M:%S}, items: {}/{}, read: {}",
                std::chrono::duration_cast<std::chrono::seconds>(stats.MatchTime()),
                stats.Match(),
                stats.Match() + stats.Miss(),
                Traits::ByteQuantity(stats.MatchReadLength()));
        }
        else
        {
            auto ruleNode = statsNode.AddNode(searchTerm->GetRule());
            ruleNode.Add(
                "Match: {:%H:%M:%S}, items: {}/{}, read: {}, collection: {:%H:%M:%S}, collection read: {}",
                std::chrono::duration_cast<std::chrono::seconds>(stats.MatchTime()),
                stats.Match(),
                stats.Match() + stats.Miss(),
                Traits::ByteQuantity(stats.MatchReadLength()),
                std::chrono::duration_cast<std::chrono::seconds>(stats.CollectionTime()),
                Traits::ByteQuantity(stats.CollectionReadLength()));
        }

        statsNode.AddEmptyLine();
    }
}

void WriteNtfsFindStatistics(
    const std::vector<std::shared_ptr<FileFind::SearchTerm>>& searchTerms,
    Orc::StructuredOutputWriter::IWriter::Ptr& writer)
{
    const auto kNodeRules = L"ntfs_find";
    writer->BeginCollection(kNodeRules);
    Guard::Scope onExit([&]() { writer->EndCollection(kNodeRules); });

    for (const auto& searchTerm : searchTerms)
    {
        writer->BeginElement(nullptr);
        Guard::Scope onExit([&]() { writer->EndElement(nullptr); });

        const auto& stats = searchTerm->GetProfilingStatistics();

        writer->WriteNamed(L"description", searchTerm->GetRule());
        writer->WriteNamed(L"match_time", std::chrono::duration_cast<std::chrono::seconds>(stats.MatchTime()).count());
        writer->WriteNamed(L"match_read", stats.MatchReadLength());
        writer->WriteNamed(L"match", stats.Match());
        writer->WriteNamed(L"miss", stats.Miss());
        writer->WriteNamed(
            L"collection_time", std::chrono::duration_cast<std::chrono::seconds>(stats.CollectionTime()).count());
        writer->WriteNamed(L"collection_read", stats.CollectionReadLength());
    }
}

Orc::Result<void> WriteStatistics(
    const std::vector<std::shared_ptr<FileFind::SearchTerm>>& searchTerms,
    Orc::StructuredOutputWriter::IWriter::Ptr& writer)
{
    try
    {
        writer->WriteNamed(L"version", L"1.0");

        const auto kNodeDfirOrc = L"dfir-orc";
        writer->BeginElement(kNodeDfirOrc);
        Guard::Scope onExit([&]() { writer->EndElement(kNodeDfirOrc); });

        {
            const auto kNodeGetthis = kToolName;
            writer->BeginElement(kNodeGetthis);
            Guard::Scope onExit([&]() { writer->EndElement(kNodeGetthis); });

            {
                const auto kNodeStats = L"statistics";
                writer->BeginElement(kNodeStats);
                Guard::Scope onExit([&]() { writer->EndElement(kNodeStats); });

                WriteNtfsFindStatistics(searchTerms, writer);
            }
        }
    }
    catch (const std::system_error& e)
    {
        return e.code();
    }
    catch (const std::exception& e)
    {
        std::cerr << "std::exception writing statistics" << std::endl;
        std::cerr << "Caught: " << e.what() << std::endl;
        std::cerr << "Type: " << typeid(e).name() << std::endl;
        return std::errc::state_not_recoverable;
    }
    catch (...)
    {
        std::cerr << "Exception during writing statistics" << std::endl;
        return std::errc::state_not_recoverable;
    }

    return Orc::Success<void>();
}

Orc::Result<void>
WriteStatistics(const std::vector<std::shared_ptr<FileFind::SearchTerm>>& searchTerms, const OutputSpec& output)
{
    auto options = std::make_unique<StructuredOutput::JSON::Options>();
    options->Encoding = OutputSpec::Encoding::UTF8;
    options->bPrettyPrint = true;

    if (output.Type != OutputSpecTypes::Kind::JSON)
    {
        Log::Error(L"Failed to write statistics, only json output is supported: {}", output.Path);
        return std::errc::invalid_argument;
    }

    auto writer = StructuredOutputWriter::GetWriter(output, std::move(options));
    if (writer == nullptr)
    {
        Log::Error(L"Failed to create writer for statistics file {}", output.Path);
        return SystemError(E_FAIL);
    }

    auto rv = WriteStatistics(searchTerms, writer);
    if (rv.has_error())
    {
        Log::Error(L"Failed to write statistics file [{}]", rv);
        return rv;
    }

    HRESULT hr = writer->Close();
    if (FAILED(hr))
    {
        const auto error = SystemError(hr);
        Log::Error(L"Failed to close statistics file [{}]", error);
        return error;
    }

    return Orc::Success<void>();
}

Orc::Result<void> WriteStatistics(
    const std::vector<std::shared_ptr<FileFind::SearchTerm>>& searchTerms,
    const std::filesystem::path& path)
{
    OutputSpec output;
    output.Type = OutputSpec::Kind::JSON;
    output.Path = path;
    return WriteStatistics(searchTerms, output);
}

Orc::Result<void> CompressStatistics(
    const std::unique_ptr<Archive::Appender<Archive::Archive7z>>& compressor,
    const std::vector<std::shared_ptr<FileFind::SearchTerm>>& searchTerms)
{
    OutputSpec output;
    output.OutputEncoding = OutputSpec::Encoding::UTF8;
    output.Configure(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON, std::wstring(kGetThisStatistics));

    auto stream = std::make_shared<TemporaryStream>();
    HRESULT hr = stream->Open(output.Path, 5 * 1024 * 1024, false);
    if (FAILED(hr))
    {
        const auto error = SystemError(hr);
        Log::Error(L"Failed to create temporary stream for statistics [{}]", error);
        return error;
    }

    auto options = std::make_unique<StructuredOutput::JSON::Options>();
    options->Encoding = output.OutputEncoding;
    options->bPrettyPrint = true;

    auto writer = StructuredOutputWriter::GetWriter(stream, output.Type, std::move(options));
    if (writer == nullptr)
    {
        Log::Error("Failed to create writer for statistics");
        return SystemError(E_FAIL);
    }

    auto rv = WriteStatistics(searchTerms, writer);
    if (rv.has_error())
    {
        return rv;
    }

    hr = writer->Close();
    if (FAILED(hr))
    {
        const auto error = SystemError(hr);
        Log::Error(L"Failed to close statistics writer [{}]", error);
        return error;
    }

    hr = stream->SetFilePointer(0, FILE_BEGIN, nullptr);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to rewind statistics stream [{}]", SystemError(hr));
    }

    auto item = std::make_unique<Archive::Item>(stream, kGetThisStatistics);
    compressor->Add(std::move(item));
    return Orc::Success<void>();
}

std::unique_ptr<Archive::Appender<Archive::Archive7z>> CreateCompressor(const OutputSpec& outputSpec)
{
    using namespace Archive;

    std::error_code ec;
    auto compressionLevel = ToCompressionLevel(outputSpec.Compression, ec);
    if (ec)
    {
        return {};
    }

    Archive::Archive7z archiver(ToArchiveFormatNg(outputSpec.ArchiveFormat), compressionLevel, outputSpec.Password);

    auto appender = Appender<Archive7z>::Create(std::move(archiver), fs::path(outputSpec.Path), 1024 * 1024 * 50, ec);
    if (ec)
    {
        return {};
    }

    return appender;
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
        Log::Error(L"Failed to create temp stream [{}]", SystemError(hr));
        return nullptr;
    }

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = encoding;

    auto csvWriter = TableOutput::CSV::Writer::MakeNew(std::move(options));
    hr = csvWriter->WriteToStream(csvStream);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to initialize CSV stream [{}]", SystemError(hr));
        return nullptr;
    }

    hr = csvWriter->SetSchema(schema);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to set CSV schema [{}]", SystemError(hr));
        return nullptr;
    }

    return csvWriter;
}

void CompressTable(
    const std::unique_ptr<Archive::Appender<Archive::Archive7z>>& compressor,
    const std::shared_ptr<TableOutput::IStreamWriter>& tableWriter)
{
    HRESULT hr = tableWriter->Flush();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to flush csv writer [{}]", SystemError(hr));
    }

    auto tableStream = tableWriter->GetStream();
    if (tableStream == nullptr || tableStream->GetSize() == 0)
    {
        return;
    }

    hr = tableStream->SetFilePointer(0, FILE_BEGIN, nullptr);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to rewind csv stream [{}]", SystemError(hr));
    }

    auto item = std::make_unique<Archive::Item>(tableStream, kGetThisCsv);
    compressor->Add(std::move(item));
}

Result<void>
WriteTable(const std::shared_ptr<TableOutput::IStreamWriter>& tableWriter, const std::filesystem::path& output)
{
    HRESULT hr = tableWriter->Flush();
    if (FAILED(hr))
    {
        auto e = SystemError(hr);
        Log::Error("Failed to flush table stream [{}]", e);
        return e;
    }

    auto temporaryStream = tableWriter->GetStream();
    if (!temporaryStream)
    {
        Log::Error("Failed to retrieve temporary stream from csv writer");
        return std::errc::bad_address;
    }

    auto stream = std::make_unique<FileStream>();
    hr = stream->OpenFile(
        output, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (FAILED(hr))
    {
        auto e = SystemError(hr);
        Log::Error(L"Failed to open output file: '{}' [{}]", output, e);
        return e;
    }

    hr = temporaryStream->CopyTo(*stream, nullptr);
    if (FAILED(hr))
    {
        auto e = SystemError(hr);
        Log::Error(L"Failed to write file: '{}' [{}]", output, e);
        return e;
    }

    return Orc::Success<void>();
}

std::wstring RetrieveComputerName(const std::wstring& defaultName)
{
    std::wstring name;

    HRESULT hr = SystemDetails::GetOrcComputerName(name);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to retrieve computer name [{}]", SystemError(hr));
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
        Log::Error(L"Failed to create sample directory [{}]", SystemError(hr));
        return hr;
    }

    FileStream outputStream;
    hr = outputStream.WriteTo(outPath.c_str());
    if (FAILED(hr))
    {
        Log::Error(L"Failed to create sample '{}' [{}]", outPath, SystemError(hr));
        return hr;
    }

    ULONGLONG ullBytesWritten = 0LL;
    hr = src.CopyTo(outputStream, &ullBytesWritten);
    if (FAILED(hr))
    {
        Log::Error(L"Failed while writing sample '{}' [{}]", outPath, SystemError(hr));
        return hr;
    }

    hr = outputStream.Close();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to close sample '{}' [{}]", outPath, SystemError(hr));
        return hr;
    }

    hr = src.Close();
    if (FAILED(hr))
    {
        Log::Warn(L"Failed to close input steam for '{}' [{}]", outPath, SystemError(hr));
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
CreateSampleFileName(const Main::SampleRef& sample, const PFILE_NAME pFileName, const std::wstring& dataName)
{
    std::array<wchar_t, 2048> name;
    int len = 0;

    const auto FRN = reinterpret_cast<const LARGE_INTEGER*>(&sample.FRN)->QuadPart;
    const auto parentFRN = reinterpret_cast<const LARGE_INTEGER*>(&pFileName->ParentDirectory)->QuadPart;
    const auto contentType = Command::GetThis::ToString(sample.Content.Type);
    const auto snapshotId = ToString(sample.SnapshotID);

    if (dataName.size())
    {
        len = swprintf_s(
            name.data(),
            name.size(),
            L"%llX_%llX_%llX_%x_%.*s_%.*s_%s.%s",
            sample.VolumeSerial,
            parentFRN,
            FRN,
            sample.InstanceID,
            pFileName->FileNameLength,
            pFileName->FileName,
            (UINT)dataName.size(),
            dataName.c_str(),
            snapshotId.c_str(),
            contentType.c_str());
    }
    else
    {
        len = swprintf_s(
            name.data(),
            name.size(),
            L"%llX_%llX_%llX_%x_%.*s_%s.%s",
            sample.VolumeSerial,
            parentFRN,
            FRN,
            sample.InstanceID,
            pFileName->FileNameLength,
            pFileName->FileName,
            snapshotId.c_str(),
            contentType.c_str());
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

    if (globalLimits.dwMaxSampleCount.has_value())
    {
        if (globalLimits.dwAccumulatedSampleCount >= globalLimits.dwMaxSampleCount.value())
        {
            return GlobalSampleCountLimitReached;
        }
    }

    if (localLimits.dwMaxSampleCount.has_value())
    {
        if (localLimits.dwAccumulatedSampleCount >= localLimits.dwMaxSampleCount.value())
        {
            return LocalSampleCountLimitReached;
        }
    }

    if (globalLimits.dwlMaxBytesPerSample.has_value())
    {
        if (dataSize > globalLimits.dwlMaxBytesPerSample.value())
        {
            return GlobalMaxBytesPerSample;
        }
    }

    if (globalLimits.dwlMaxTotalBytes.has_value())
    {
        if (dataSize + globalLimits.dwlAccumulatedBytesTotal > globalLimits.dwlMaxTotalBytes.value())
        {
            return GlobalMaxTotalBytes;
        }
    }

    if (localLimits.dwlMaxBytesPerSample.has_value())
    {
        if (dataSize > localLimits.dwlMaxBytesPerSample.value())
        {
            return LocalMaxBytesPerSample;
        }
    }

    if (localLimits.dwlMaxTotalBytes.has_value())
    {
        if (dataSize + localLimits.dwlAccumulatedBytesTotal > localLimits.dwlMaxTotalBytes.value())
        {
            return LocalMaxTotalBytes;
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
    const Main::SampleRef& sample,
    const std::wstring& qualifier,
    const PFILE_NAME pFileName,
    const std::wstring& dataName)
{
    _ASSERT(pFileName);
    if (pFileName == nullptr)
    {
        return {};
    }

    std::wstring name;
    std::unordered_set<std::wstring>::iterator it;

    name = ::CreateSampleFileName(sample, pFileName, dataName);
    if (!qualifier.empty())
    {
        name.insert(0, L"\\");
        name.insert(0, qualifier);
    }

    return name;
}

HRESULT RegFlushKeys()
{
    bool bSuccess = true;
    DWORD dwGLE = 0L;

    Log::Debug("Flushing HKEY_LOCAL_MACHINE");
    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
    {
        bSuccess = false;
    }

    Log::Debug("Flushing HKEY_USERS");
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

PFILE_NAME GetLongestFileName(const std::vector<Orc::FileFind::Match::NameMatch>& nameMatches)
{
    PFILE_NAME name = nullptr;

    for (const auto& match : nameMatches)
    {
        if (!name || name->FileNameLength < match.FILENAME()->FileNameLength)
        {
            name = match.FILENAME();
        }
    }

    return name;
}

PFILE_NAME GetLastFileName(const std::vector<Orc::FileFind::Match::NameMatch>& nameMatches)
{
    if (nameMatches.empty())
    {
        return nullptr;
    }

    return nameMatches[nameMatches.size() - 1].FILENAME();
}

}  // namespace

GUID Main::SampleId::GetSnapshotId(VolumeReader& volumeReader)
{
    VolumeReaderInfo volumeInfo;
    volumeReader.Accept(volumeInfo);
    return volumeInfo.Guid();
}

Main::Main()
    : UtilitiesMain()
    , config()
    , FileFinder(true, Orc::CryptoHashStreamAlgorithm::Undefined, false)
    , CollectionDate()
    , ComputerName(::RetrieveComputerName(L"Default"))
{
}

std::unique_ptr<Main::SampleRef> Main::CreateSample(
    const std::shared_ptr<FileFind::Match>& match,
    const size_t attributeIndex,
    const SampleSpec& sampleSpec) const
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
    sample->SourcePath = ::GetMatchFullName(match->MatchingNames.front(), attribute);

    sample->isRecordInUse = !match->DeletedRecord;

    // Keep 10.0 behavior for compatibility, eventually use ::GetLongestFileName when compatibility can be broken and
    // remove parentFRN to only keep FRN in the filename
    const auto lastFileName = ::GetLastFileName(match->MatchingNames);
    if (!lastFileName)
    {
        Log::Error(L"Failed to retrieve sample file name for '{}'", sample->SampleName);
        return nullptr;
    }

    sample->SampleName = ::CreateUniqueSampleName(*sample, sampleSpec.Name, lastFileName, attribute.AttrName);

    HRESULT hr = ConfigureSampleStreams(*sample);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to configure sample reference for '{}' [{}]", sample->SampleName, SystemError(hr));
    }

    return sample;
}

HRESULT Main::ConfigureSampleStreams(SampleRef& sample) const
{
    HRESULT hr = E_FAIL;

    _ASSERT(sample.Matches.front()->MatchingAttributes[sample.AttributeIndex].DataStream->IsOpen() == S_OK);

    const auto& dataStream = sample.Matches.front()->MatchingAttributes[sample.AttributeIndex].DataStream;
    hr = dataStream->SetFilePointer(0, FILE_BEGIN, NULL);
    if (FAILED(hr))
    {
        return hr;
    }

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

    // WORKAROUND: some stream like wof can use a very large context for fast I/O. By design it is difficult to
    // close/reopen streams. When 'ShrinkContext' is implemented it allows to free resources until next use.
    hr = dataStream->ShrinkContext();
    if (FAILED(hr))
    {
        Log::Error("Failed to shrink stream context [{}]", SystemError(hr));
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

            output.WriteNothing();

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

            output.WriteBool(sample.isRecordInUse);

            output.WriteEndOfLine();
        }
    }

    return S_OK;
}

HRESULT Main::WriteSample(
    Archive::Appender<Archive::Archive7z>& compressor,
    std::unique_ptr<SampleRef> pSample,
    SampleWrittenCb writtenCb) const
{
    auto sample = std::shared_ptr<SampleRef>(std::move(pSample));

    auto onItemArchivedCb = [this, sample, writtenCb](const std::error_code& ec) {
        FinalizeHashes(*sample);

        HRESULT hrTable = AddSampleRefToCSV(*m_tableWriter, *sample);
        if (FAILED(hrTable))
        {
            Log::Error(
                L"Failed to add sample '{}' metadata to csv [{}]",
                sample->Matches.front()->MatchingNames.front().FullPathName,
                SystemError(hrTable));
        }

        if (writtenCb)
        {
            HRESULT hr = E_FAIL;
            if (SUCCEEDED(hrTable) && !ec)
            {
                hr = S_OK;
            }

            writtenCb(*sample, hr);
        }
    };

    if (sample->IsOfflimits())
    {
        onItemArchivedCb({});
        return S_OK;
    }

    auto item = std::make_unique<Archive::Item>(sample->CopyStream, sample->SampleName, std::move(onItemArchivedCb));
    compressor.Add(std::move(item));

    return S_OK;
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
            Log::Error(L"Failed to copy stream of '{}' [{}]", sampleFile, SystemError(hrCopy));
        }
    }

    FinalizeHashes(*sample);

    hrCsv = AddSampleRefToCSV(*m_tableWriter, *sample);
    if (FAILED(hrCsv))
    {
        Log::Error(
            L"Failed to add sample '{}' metadata to csv [{}]",
            sample->Matches.front()->MatchingNames.front().FullPathName,
            SystemError(hrCsv));
    }

    if ((SUCCEEDED(hrCopy) || sample->IsOfflimits()) && SUCCEEDED(hrCsv))
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
            Log::Error(L"Failed while computing hash of '{}' [{}]", sample.SampleName, SystemError(hr));
        }

        sample.CopyStream->Close();
    }

    sample.HashStream->GetMD5(const_cast<CBinaryBuffer&>(sample.MD5));
    sample.HashStream->GetSHA1(const_cast<CBinaryBuffer&>(sample.SHA1));
    sample.HashStream->GetSHA256(const_cast<CBinaryBuffer&>(sample.SHA256));

    if (sample.FuzzyHashStream)
    {
        sample.FuzzyHashStream->GetSSDeep(const_cast<CBinaryBuffer&>(sample.SSDeep));
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
            Log::Warn(L"Failed to resolve current working directory [{}]", ec.value());
        }
    }

    m_compressor = ::CreateCompressor(config.Output);
    if (m_compressor == nullptr)
    {
        Log::Error(L"Failed to create compressor");
        return hr;
    }

    OutputSpec output;
    output.Schema = config.Output.Schema;
    output.OutputEncoding = config.Output.OutputEncoding;
    output.Configure(L"GetThisTemporaryStream.csv", tempDir);

    m_tableWriter = ::CreateCsvWriter(output.Path, config.Output.Schema, config.Output.OutputEncoding, hr);
    if (m_tableWriter == nullptr)
    {
        Log::Error(L"Failed to create csv stream [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::CloseArchiveOutput()
{
    _ASSERT(m_compressor);
    _ASSERT(m_tableWriter);

    std::error_code ec;

    m_compressor->Flush(ec);
    if (ec)
    {
        Log::Error(L"Failed to compress '{}' [{}]", config.Output.Path, ec);
    }

    ::CompressTable(m_compressor, m_tableWriter);

    auto rv = ::CompressStatistics(m_compressor, FileFinder.AllSearchTerms());
    if (rv.has_error())
    {
        Log::Error(L"Failed to write statistics file [{}]", rv);
    }

    m_compressor->Close(ec);
    if (ec)
    {
        Log::Error(L"Failed to close archive [{}]", ec.value());
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
        Log::Error(L"Failed to create output directory [{}]", SystemError(hr));
        return hr;
    }

    m_tableWriter = ::CreateCsvWriter(outputDir / kGetThisCsv, config.Output.Schema, config.Output.OutputEncoding, hr);
    if (m_tableWriter == nullptr)
    {
        Log::Error(L"Failed to create csv stream [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::CloseDirectoryOutput()
{
    const auto tablePath = std::filesystem::path(config.Output.Path) / kGetThisCsv;
    auto rv = ::WriteTable(m_tableWriter, tablePath);
    if (rv.has_error())
    {
        Log::Critical(L"Failed to write: '{}' [{}]", tablePath, rv.error());
        return ToHRESULT(rv.error());
    }

    const auto statisticsPath = std::filesystem::path(config.Output.Path) / kGetThisStatistics;
    rv = ::WriteStatistics(FileFinder.AllSearchTerms(), statisticsPath);
    if (rv.has_error())
    {
        Log::Error(L"Failed to write: '{}' [{}]", statisticsPath, rv.error());
        // return ToHRESULT(rv.error());
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

        case GlobalMaxTotalBytes:
            GlobalLimits.bMaxTotalBytesReached = true;
            break;

        case LocalSampleCountLimitReached:
            sampleSpec.PerSampleLimits.bMaxSampleCountReached = true;
            break;

        case LocalMaxBytesPerSample:
            sampleSpec.PerSampleLimits.bMaxBytesPerSampleReached = true;
            break;

        case LocalMaxTotalBytes:
            sampleSpec.PerSampleLimits.bMaxTotalBytesReached = true;
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
        Log::Error(L"[FAILED] '{}', {} bytes [{}]", name, sample.SampleSize, SystemError(hrWrite));
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

        case GlobalMaxTotalBytes:
            m_console.Print(L"{}: Global total sample size limit reached ({})", name, GlobalLimits.dwlMaxTotalBytes);
            break;

        case LocalSampleCountLimitReached:
            m_console.Print(L"{}: sample count reached ({})", name, sampleSpec.PerSampleLimits.dwMaxSampleCount);
            break;

        case LocalMaxBytesPerSample:
            m_console.Print(
                L"{}: Exceeds per sample size limit ({})", name, sampleSpec.PerSampleLimits.dwlMaxBytesPerSample);
            break;

        case LocalMaxTotalBytes:
            m_console.Print(
                L"{}: total sample size limit reached ({})", name, sampleSpec.PerSampleLimits.dwlMaxTotalBytes);
            break;

        case FailedToComputeLimits:
            break;
    }
}

void Main::OnMatchingSample(const std::shared_ptr<FileFind::Match>& aMatch, bool& bStop)
{
    HRESULT hr = E_FAIL;

    _ASSERT(aMatch != nullptr);

    if (aMatch->MatchingAttributes.empty())
    {
        Log::Warn(
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

        if (m_sampleIds.find(SampleId(*aMatch, i)) != std::cend(m_sampleIds))
        {
            Log::Warn(
                L"FRN already processed, sample '{}' will not be added to the archive",
                aMatch->MatchingNames.front().FullPathName);
            continue;
        }

        auto sample = CreateSample(aMatch, i, sampleSpec);
        UpdateSamplesLimits(sampleSpec, *sample);

        // TODO: memory optimization: check that sampleIds is reset when volume changes
        m_sampleIds.insert(SampleId(*sample));

        if (config.Output.Type == OutputSpec::Kind::Archive)
        {
            hr =
                WriteSample(*m_compressor, std::move(sample), [this, &sampleSpec](const SampleRef& sample, HRESULT hr) {
                    OnSampleWritten(sample, sampleSpec, hr);
                });
        }
        else if (config.Output.Type == OutputSpec::Kind::Directory)
        {
            hr = WriteSample(
                config.Output.Path, std::move(sample), [this, &sampleSpec](const SampleRef& sample, HRESULT hr) {
                    OnSampleWritten(sample, sampleSpec, hr);
                });
        }

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
        Log::Error(L"Failed to initialize Yara [{}]", SystemError(hr));
        return hr;
    }

    hr = FileFinder.Find(
        config.Locations,
        std::bind(&Main::OnMatchingSample, this, std::placeholders::_1, std::placeholders::_2),
        false,
        config.resurrectRecordsMode);

    if (FAILED(hr))
    {
        Log::Error(L"Failed while parsing locations");
    }

    m_console.PrintNewLine();
    ::PrintStatistics(m_console.OutputTree(), FileFinder.AllSearchTerms());

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
            Log::Error(L"Cannot close archive output [{}]", SystemError(hr));
            return hr;
        }
    }
    else if (config.Output.Type == OutputSpec::Kind::Directory)
    {
        hr = CloseDirectoryOutput();
        if (FAILED(hr))
        {
            Log::Error(L"Cannot close directory output [{}]", SystemError(hr));
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
                Log::Error(L"Failed to flush keys [{}]", SystemError(hr));
                return hr;
            }
        }

        hr = InitOutput();
        if (FAILED(hr))
        {
            Log::Error(L"Cannot initialize output mode [{}]", SystemError(hr));
            return hr;
        }

        hr = FindMatchingSamples();
        if (FAILED(hr))
        {
            Log::Error(L"GetThis failed while matching samples [{}]", SystemError(hr));
            return hr;
        }

        hr = CloseOutput();
        if (FAILED(hr))
        {
            Log::Error(L"Failed to close output [{}]", SystemError(hr));
        }
    }
    catch (...)
    {
        Log::Error(L"GetThis failed during sample collection, terminating archive");
        return E_UNEXPECTED;
    }

    return S_OK;
}
