//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "PeParser.h"

#include "ByteStreamHelpers.h"
#include "CryptoHashStream.h"
#include "BinaryBuffer.h"
#include "SubStream.h"
#include "Stream/StreamUtils.h"

#ifndef IMAGE_FILE_MACHINE_TARGET_HOST
#    define IMAGE_FILE_MACHINE_TARGET_HOST 0x0001
#endif

#ifndef IMAGE_FILE_MACHINE_ARM64
#    define IMAGE_FILE_MACHINE_ARM64 0xAA64
#endif

#ifndef IMAGE_FILE_MACHINE_ARMNT
#    define IMAGE_FILE_MACHINE_ARMNT 0x01C4
#endif

using namespace Orc;

namespace {

#pragma pack(push, 1)
// Like IMAGE_NT_HEADERS32/IMAGE_NT_HEADERS64 but keeps only fixed fields size
struct ImageNtHeaderFixedSize
{
    DWORD Signature;
    IMAGE_FILE_HEADER FileHeader;
};
#pragma pack(pop)

uint64_t CopyChunk(ByteStream& input, const PeParser::PeChunk& chunk, ByteStream& output, std::error_code& ec)
{
    size_t processed = 0;

    std::array<uint8_t, 16384> buffer;

    while (processed < chunk.length)
    {
        auto span = BufferSpan(buffer.data(), std::min(buffer.size(), chunk.length - processed));

        const auto lastReadLength = ReadChunkAt(input, chunk.offset + processed, span, ec);
        if (ec)
        {
            return processed;
        }

        WriteChunk(output, BufferView(buffer.data(), lastReadLength), ec);
        if (ec)
        {
            return processed;
        }

        processed += lastReadLength;
    }

    return processed;
}

uint64_t CopyChunks(const PeParser::PeChunks& chunks, ByteStream& input, ByteStream& output, std::error_code& ec)
{
    uint64_t processed = 0;

    for (const auto& chunk : chunks)
    {
        processed += CopyChunk(input, chunk, output, ec);
        if (ec)
        {
            Log::Debug(
                "Failed to hash chunk (offset: {}, length: {}) [{}]", Traits::Offset(chunk.offset), chunk.length, ec);
            return processed;
        }
    }

    return processed;
}

void ParseImageDosHeader(ByteStream& stream, IMAGE_DOS_HEADER& header, std::error_code& ec)
{
    ReadItemAt(stream, 0, header, ec);
    if (ec)
    {
        return;
    }

    switch (header.e_magic)
    {
        case IMAGE_DOS_SIGNATURE:
            break;
        case IMAGE_OS2_SIGNATURE:
        case IMAGE_OS2_SIGNATURE_LE:
        default:
            ec = std::make_error_code(std::errc::bad_message);
            return;
    }
}

void ParseImageNtHeaders(
    ByteStream& stream,
    const IMAGE_DOS_HEADER& imageDosHeader,
    std::optional<IMAGE_FILE_HEADER>& imageFileHeader,
    std::optional<PeParser::OptionalHeader>& imageOptionalHeader,
    std::error_code& ec)
{
    ImageNtHeaderFixedSize imageNtHeader;

    ReadItemAt(stream, imageDosHeader.e_lfanew, imageNtHeader, ec);
    if (ec)
    {
        Log::Debug("Failed to read IMAGE_NT_HEADER [{}]", ec);
        return;
    }

    if (imageNtHeader.Signature != IMAGE_NT_SIGNATURE)
    {
        const uint16_t signature = LOWORD(imageNtHeader.Signature);
        if (signature == IMAGE_DOS_SIGNATURE || signature == IMAGE_OS2_SIGNATURE || signature == IMAGE_OS2_SIGNATURE_LE
            || signature == IMAGE_VXD_SIGNATURE)
        {
            return;
        }

        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug("Invalid IMAGE_NT_SIGNATURE (value: {:#x})", imageNtHeader.Signature);
        return;
    }

    imageFileHeader = imageNtHeader.FileHeader;

    switch (imageFileHeader->Machine)
    {
        case IMAGE_FILE_MACHINE_I386:
        case IMAGE_FILE_MACHINE_ARM:
        case IMAGE_FILE_MACHINE_ARMNT:
        case IMAGE_FILE_MACHINE_THUMB:
        case IMAGE_FILE_MACHINE_POWERPC:
        case IMAGE_FILE_MACHINE_POWERPCFP:
        case IMAGE_FILE_MACHINE_MIPSFPU:
        case IMAGE_FILE_MACHINE_MIPSFPU16:
        case IMAGE_FILE_MACHINE_R3000:
        case IMAGE_FILE_MACHINE_R4000:
        case IMAGE_FILE_MACHINE_R10000:
        case IMAGE_FILE_MACHINE_WCEMIPSV2:
        case IMAGE_FILE_MACHINE_SH3:
        case IMAGE_FILE_MACHINE_SH3DSP:
        case IMAGE_FILE_MACHINE_SH3E:
        case IMAGE_FILE_MACHINE_SH4:
        case IMAGE_FILE_MACHINE_SH5:
        case IMAGE_FILE_MACHINE_M32R:
        case IMAGE_FILE_MACHINE_AM33:
        case IMAGE_FILE_MACHINE_TRICORE:
        case IMAGE_FILE_MACHINE_CEF:
        case IMAGE_FILE_MACHINE_CEE:
        case IMAGE_FILE_MACHINE_MIPS16:
        case IMAGE_FILE_MACHINE_UNKNOWN:
        case IMAGE_FILE_MACHINE_TARGET_HOST:
        case IMAGE_FILE_MACHINE_EBC:
        case IMAGE_FILE_MACHINE_ALPHA: {
            IMAGE_OPTIONAL_HEADER32 optionalHeader32 = {0};
            ReadItem(stream, optionalHeader32, ec);
            if (ec)
            {
                Log::Debug("Failed to read IMAGE_OPTIONAL_HEADER32 [{}]", ec);
                return;
            }

            if (optionalHeader32.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            {
                Log::Debug("Invalid IMAGE_NT_OPTIONAL_HDR32_MAGIC (value: {})", optionalHeader32.Magic);
                return;
            }

            imageOptionalHeader = optionalHeader32;
            break;
        }
        case IMAGE_FILE_MACHINE_AMD64:
        case IMAGE_FILE_MACHINE_IA64:
        case IMAGE_FILE_MACHINE_ARM64:
        case IMAGE_FILE_MACHINE_ALPHA64: {
            IMAGE_OPTIONAL_HEADER64 optionalHeader64 = {0};
            ReadItem(stream, optionalHeader64, ec);
            if (ec)
            {
                Log::Debug("Failed to read IMAGE_OPTIONAL_HEADER64 [{}]", ec);
                return;
            }

            if (optionalHeader64.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                Log::Debug("Invalid IMAGE_NT_OPTIONAL_HDR64_MAGIC (value: {})", optionalHeader64.Magic);
                return;
            }

            imageOptionalHeader = optionalHeader64;
            break;
        }
        default:
            Log::Debug("Unsupported machine (value: {})", imageFileHeader->Machine);
            ec = std::make_error_code(std::errc::function_not_supported);
            return;
    }
}

// BEWARE: this function expects the stream to be at the right file position (just after optional headers)
void ParseImageSections(
    ByteStream& stream,
    const IMAGE_FILE_HEADER& imageFileHeader,
    PeParser::SectionHeaders& imageSectionHeader,
    std::error_code& ec)
{
    const auto kMaxSectionCount = 96;

    if (imageFileHeader.NumberOfSections > kMaxSectionCount)
    {
        // Log as error as this could be malicious behavior
        Log::Error("Invalid PE with too many section (value: {})", imageFileHeader.NumberOfSections);
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    imageSectionHeader.resize(imageFileHeader.NumberOfSections);
    ReadChunk(stream, imageSectionHeader, ec);
    if (ec)
    {
        Log::Debug("Failed to read sections [{}]", ec);
        return;
    }
}

struct ResourceDirectory
{
    IMAGE_RESOURCE_DIRECTORY header;
    std::vector<IMAGE_RESOURCE_DIRECTORY_ENTRY> entries;
};

Result<ResourceDirectory> ParseResourceDirectory(ByteStream& stream, uint64_t offset)
{
    std::error_code ec;

    IMAGE_RESOURCE_DIRECTORY directory;
    ReadItemAt(stream, offset, directory, ec);
    if (ec)
    {
        Log::Debug("Failed to read resource directory [{}]", ec);
        return ec;
    }

    const size_t numberOfEntries =
        static_cast<size_t>(directory.NumberOfIdEntries) + static_cast<size_t>(directory.NumberOfNamedEntries);

    std::vector<IMAGE_RESOURCE_DIRECTORY_ENTRY> entries(numberOfEntries);
    ReadChunk(stream, entries, ec);
    if (ec)
    {
        Log::Debug("Failed to read resource directory entries [{}]", ec);
        return ec;
    }

    return ResourceDirectory {directory, std::move(entries)};
}

Result<std::wstring> ParseEntryName(ByteStream& stream, const IMAGE_RESOURCE_DIRECTORY_ENTRY& entry)
{
    if (!entry.NameIsString)
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    std::error_code ec;

    IMAGE_RESOURCE_DIR_STRING_U stringHeader;
    ReadItemAt(stream, entry.NameOffset, stringHeader, ec);
    if (ec)
    {
        Log::Debug("Failed to read resource name header [{}]", ec);
        return ec;
    }

    const uint64_t nameOffset = entry.NameOffset + sizeof(IMAGE_RESOURCE_DIR_STRING_U::Length);
    std::wstring name(stringHeader.Length, L'\0');
    ReadChunkAt(
        stream,
        nameOffset,
        BufferSpan(reinterpret_cast<uint8_t*>(name.data()), stringHeader.Length * sizeof(wchar_t)),
        ec);
    if (ec)
    {
        Log::Debug("Failed to read resource name string [{}]", ec);
        return ec;
    }

    return name;
}

Result<IMAGE_RESOURCE_DIRECTORY_ENTRY> GetChildResourceDirectory(
    ByteStream& stream,
    std::optional<uint64_t> parentOffset,
    const std::variant<WORD, std::wstring_view>& nameOrId)
{
    auto resourceDir = ParseResourceDirectory(stream, parentOffset.value_or(0));
    if (!resourceDir)
    {
        return resourceDir.error();
    }

    for (const auto& entry : resourceDir->entries)
    {
        if (!entry.NameIsString)
        {
            auto id = std::get_if<WORD>(&nameOrId);
            if (id == nullptr || entry.Id != *id)
            {
                continue;
            }

            return entry;
        }
        else
        {
            auto name = std::get_if<std::wstring_view>(&nameOrId);
            if (name == nullptr)
            {
                continue;
            }

            auto entryName = ParseEntryName(stream, entry);
            if (!entryName)
            {
                Log::Debug("Failed to parse entry name [{}]", entryName.error());
                continue;
            }

            if (*entryName == *name)
            {
                return entry;
            }
        }
    }

    return std::errc::no_such_file_or_directory;
}

Result<IMAGE_RESOURCE_DATA_ENTRY> GetResourceDataEntry(ByteStream& stream, uint64_t offsetToData)
{
    std::error_code ec;

    const uint64_t dataEntryOffset = offsetToData & 0x7FFFFFFF;

    IMAGE_RESOURCE_DATA_ENTRY dataEntry;
    ReadItemAt(stream, dataEntryOffset, dataEntry, ec);
    if (ec)
    {
        Log::Debug("Failed to read resource data entry [{}]", ec);
        return ec;
    }

    return dataEntry;
}

Result<IMAGE_RESOURCE_DATA_ENTRY> GetResourceDataEntry(
    ByteStream& stream,
    const std::variant<WORD, std::wstring_view>& type,
    const std::variant<WORD, std::wstring_view>& name,
    std::optional<WORD> lang)
{
    auto typeEntry = GetChildResourceDirectory(stream, {}, type);
    if (!typeEntry)
    {
        Log::Debug("Failed to find resource type entry [{}]", typeEntry.error());
        return typeEntry.error();
    }

    if (!typeEntry->DataIsDirectory)
    {
        Log::Debug("Type entry is not a directory");
        return std::errc::bad_message;
    }

    auto nameEntry = GetChildResourceDirectory(stream, static_cast<size_t>(typeEntry->OffsetToDirectory), name);
    if (!nameEntry)
    {
        Log::Debug("Failed to find name entry [{}]", nameEntry.error());
        return nameEntry.error();
    }

    if (!nameEntry->DataIsDirectory)
    {
        Log::Debug("Name entry is not a directory");
        return std::errc::bad_message;
    }

    IMAGE_RESOURCE_DIRECTORY_ENTRY langEntry;
    if (lang)
    {
        auto langEntryResult =
            GetChildResourceDirectory(stream, static_cast<size_t>(nameEntry->OffsetToDirectory), *lang);
        if (!langEntryResult)
        {
            Log::Debug("Failed to find lang entry [{}]", langEntryResult.error());
            return langEntryResult.error();
        }

        langEntry = *langEntryResult;
    }
    else
    {
        auto langDir = ParseResourceDirectory(stream, nameEntry->OffsetToDirectory);
        if (!langDir)
        {
            Log::Debug("Failed to find lang directory [{}]", langDir.error());
            return langDir.error();
        }

        if (langDir->entries.empty())
        {
            Log::Debug("Failed to find lang entry");
            return std::errc::no_such_file_or_directory;
        }

        langEntry = langDir->entries[0];
    }

    if (langEntry.DataIsDirectory)
    {
        Log::Debug("Lang entry is a directory, expected data");
        return std::errc::bad_message;
    }

    return GetResourceDataEntry(stream, langEntry.OffsetToData);
}

inline Result<std::variant<WORD, std::wstring>>
GetResourceEntryNameOrId(ByteStream& stream, const IMAGE_RESOURCE_DIRECTORY_ENTRY& entry)
{
    if (entry.NameIsString)
    {
        auto name = ParseEntryName(stream, entry);
        if (!name)
        {
            return name.error();
        }

        return *name;
    }
    else
    {
        return entry.Id;
    }
}

inline bool IsFiltered(
    const std::variant<std::monostate, WORD, std::wstring_view>& filter,
    const std::variant<WORD, std::wstring>& value)
{
    if (std::holds_alternative<std::monostate>(filter))
    {
        return false;
    }

    return std::visit(
        [&](const auto& filterValue) {
            using FilterType = std::decay_t<decltype(filterValue)>;

            if constexpr (std::is_same_v<FilterType, std::monostate>)
            {
                return false;
            }
            else if constexpr (std::is_same_v<FilterType, WORD>)
            {
                const WORD* valuePtr = std::get_if<WORD>(&value);
                if (valuePtr == nullptr)
                {
                    return true;
                }
                return filterValue != *valuePtr;
            }
            else if constexpr (std::is_same_v<FilterType, std::wstring_view>)
            {
                const std::wstring* valuePtr = std::get_if<std::wstring>(&value);
                if (valuePtr == nullptr)
                {
                    return true;
                }
                return filterValue != *valuePtr;
            }
        },
        filter);
}

std::optional<IMAGE_DATA_DIRECTORY> ResolveDataDirectory(
    const std::variant<IMAGE_OPTIONAL_HEADER32, IMAGE_OPTIONAL_HEADER64>& optionalHeader,
    uint8_t index)
{
    return std::visit(
        [index](const auto& header) -> std::optional<IMAGE_DATA_DIRECTORY> {
            if (index >= header.NumberOfRvaAndSizes)
            {
                Log::Debug(
                    "Directory index {} exceeds NumberOfRvaAndSizes {}, resolving anyway",
                    index,
                    header.NumberOfRvaAndSizes);
            }

            return header.DataDirectory[index];
        },
        optionalHeader);
}

}  // namespace

namespace Orc {

PeParser::PeParser(ByteStream& stream, std::error_code& ec)
    : m_stream(stream)
    , m_streamSize(m_stream.GetSize())
{
    HRESULT hr = stream.SetFilePointer(0, FILE_BEGIN, NULL);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        return;
    }

    ::ParseImageDosHeader(m_stream, m_imageDosHeader, ec);
    if (ec)
    {
        return;
    }

    ::ParseImageNtHeaders(m_stream, m_imageDosHeader, m_imageFileHeader, m_imageOptionalHeader, ec);
    if (ec)
    {
        return;
    }

    if (m_imageFileHeader)
    {
        ::ParseImageSections(m_stream, *m_imageFileHeader, m_imageSectionsHeaders, ec);
        if (ec)
        {
            m_imageSectionsHeaders.clear();
            ec.clear();
            return;
        }
    }
}

const IMAGE_DOS_HEADER& PeParser::ImageDosHeader() const
{
    return m_imageDosHeader;
}

const std::optional<IMAGE_FILE_HEADER>& PeParser::ImageFileHeader() const
{
    return m_imageFileHeader;
}

const std::optional<PeParser::OptionalHeader>& PeParser::ImageOptionalHeader() const
{
    return m_imageOptionalHeader;
}

Result<uint64_t> PeParser::ImageRvaToFileOffset(uint32_t rva, std::optional<size_t> chunkSizeForValidation) const
{
    for (const auto& section : m_imageSectionsHeaders)
    {
        const auto sectionEndRva =
            static_cast<uint64_t>(section.VirtualAddress) + static_cast<uint64_t>(section.Misc.VirtualSize);
        const auto sectionStartRva = static_cast<uint64_t>(section.VirtualAddress);

        if (rva >= sectionStartRva && rva < sectionEndRva)
        {
            const uint32_t offsetInSection = rva - sectionStartRva;
            if (offsetInSection >= section.SizeOfRawData)
            {
                Log::Debug("RVA {:#x} maps beyond section's raw data", rva);
                return std::make_error_code(std::errc::bad_message);
            }

            if (chunkSizeForValidation)
            {
                const auto remainingSectionSize = section.SizeOfRawData - offsetInSection;
                if (*chunkSizeForValidation > remainingSectionSize)
                {
                    Log::Debug(
                        "RVA {:#x} with chunk size {} exceeds section's raw data (remaining size: {})",
                        rva,
                        *chunkSizeForValidation,
                        remainingSectionSize);
                    return std::make_error_code(std::errc::bad_message);
                }
            }

            return static_cast<uint64_t>(section.PointerToRawData) + offsetInSection;
        }
    }

    Log::Debug("RVA {:#x} not found in any section", rva);
    return std::make_error_code(std::errc::bad_address);
}

bool PeParser::HasSecurityDirectory() const
{
    return HasImageDataDirectory(IMAGE_DIRECTORY_ENTRY_SECURITY);
}

bool PeParser::HasResourceDirectory() const
{
    return HasImageDataDirectory(IMAGE_DIRECTORY_ENTRY_RESOURCE);
}

bool PeParser::HasDebugDirectory() const
{
    return HasImageDataDirectory(IMAGE_DIRECTORY_ENTRY_DEBUG);
}

bool PeParser::HasImageDataDirectory(uint8_t index) const
{
    if (index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
    {
        return false;
    }

    if (!m_imageOptionalHeader.has_value())
    {
        return false;
    }

    const auto data = ResolveDataDirectory(*m_imageOptionalHeader, index);
    return data.has_value() && data->Size != 0;
}

IMAGE_DATA_DIRECTORY PeParser::GetImageDataDirectory(uint8_t index, std::error_code& ec) const
{
    if (index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        Log::Debug("Invalid directory (index: {}, expected: <{})", index, IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
        return {};
    }

    if (!m_imageOptionalHeader.has_value())
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        Log::Debug("Optional header not populated, cannot read DataDirectory[{}]", index);
        return {};
    }

    const auto data = ResolveDataDirectory(*m_imageOptionalHeader, index);

    if (!data.has_value() || data->Size == 0)
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }

    return *data;
}

Result<void> PeParser::ReadDirectory(uint8_t index, std::vector<uint8_t>& buffer, std::optional<size_t> maxSize) const
{
    std::error_code ec;

    const auto directory = GetImageDataDirectory(index, ec);
    if (ec)
    {
        return SystemError(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    }

    uint64_t fileOffset = 0;
    if (index != IMAGE_DIRECTORY_ENTRY_SECURITY)
    {
        auto directoryOffset = ImageRvaToFileOffset(directory.VirtualAddress);
        if (!directoryOffset)
        {
            Log::Debug("Invalid directory virtual address");
            ec = directoryOffset.error();
            return ec;
        }

        fileOffset = *directoryOffset;
    }
    else
    {
        fileOffset = directory.VirtualAddress;
    }

    if (maxSize && directory.Size > *maxSize)
    {
        Log::Debug(
            "Directory size exceeds maximum allowed (index: {}, size: {}, maxSize: {})",
            index,
            directory.Size,
            *maxSize);
        return SystemError(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    }

    ReadChunkAt(m_stream, fileOffset, directory.Size, buffer, ec);
    if (ec)
    {
        Log::Debug("Failed to read directory (index: {}) [{}]", index, ec);
        return ec;
    }

    return Orc::Success<void>();
}

Result<PeParser::PeChunk> PeParser::GetDirectoryChunk(uint8_t index, bool checkChunkBoundaries) const
{
    std::error_code ec;

    auto directory = GetImageDataDirectory(index, ec);
    if (ec)
    {
        Log::Debug("Failed to retrieve directory (index: {}) [{}]", index, ec);
        return ec;
    }

    uint64_t fileOffset = 0;
    if (index != IMAGE_DIRECTORY_ENTRY_SECURITY)
    {
        auto directoryOffset = ImageRvaToFileOffset(directory.VirtualAddress);
        if (!directoryOffset)
        {
            Log::Debug("Invalid directory virtual address");
            return directoryOffset.error();
        }

        fileOffset = *directoryOffset;
    }
    else
    {
        fileOffset = directory.VirtualAddress;
    }

    // BEWARE: In the wild some pe files have invalid resource size that overlap the file size but data is actually
    // present like 'FileSyncShell.dll' from Microsoft. So we do not validate here that the size is within the file
    // size.
    if (!checkChunkBoundaries)
    {
        return PeChunk {fileOffset, directory.Size};
    }

    if (fileOffset >= m_streamSize)
    {
        Log::Debug("Invalid directory file offset (value: {:#x}, stream size: {:#x}", fileOffset, m_streamSize);
        return std::make_error_code(std::errc::bad_message);
    }

    if (directory.Size > m_streamSize - fileOffset)
    {
        Log::Debug(
            "Invalid directory file offset or length (offset: {:#x}, length: {:#x}, stream size: {:#x}",
            fileOffset,
            directory.Size,
            m_streamSize);
    }

    return PeChunk {
        fileOffset, std::min(static_cast<size_t>(directory.Size), static_cast<size_t>(m_streamSize - fileOffset))};
}

Result<void> PeParser::ReadSecurityDirectory(std::vector<uint8_t>& buffer, std::optional<size_t> maxSize) const
{
    auto rv = ReadDirectory(IMAGE_DIRECTORY_ENTRY_SECURITY, buffer, maxSize);
    if (!rv)
    {
        Log::Debug("Failed to read IMAGE_DIRECTORY_ENTRY_SECURITY [{}]", rv.error());
        return rv.error();
    }

    return Success<void>();
}

Result<PeParser::PeChunk> PeParser::GetSecurityDirectoryChunk() const
{
    return GetDirectoryChunk(IMAGE_DIRECTORY_ENTRY_SECURITY);
}

Result<void> PeParser::ReadResourceDirectory(std::vector<uint8_t>& buffer, std::optional<size_t> maxSize) const
{
    auto rv = ReadDirectory(IMAGE_DIRECTORY_ENTRY_RESOURCE, buffer, maxSize);
    if (!rv)
    {
        Log::Debug("Failed to read IMAGE_DIRECTORY_ENTRY_RESOURCE [{}]", rv.error());
        return rv.error();
    }

    return Success<void>();
}

Result<PeParser::PeChunk> PeParser::GetResourceDirectoryChunk() const
{
    // Do not check chunk boundaries here: in the wild some pe files have invalid resource size that overlap the file
    // size but data is actually present like 'FileSyncShell.dll' from Microsoft
    auto chunk = GetDirectoryChunk(IMAGE_DIRECTORY_ENTRY_RESOURCE, false);
    if (!chunk)
    {
        return chunk.error();
    }

    if (chunk->offset > m_streamSize)
    {
        return std::errc::bad_message;
    }

    if (chunk->length > m_streamSize - chunk->offset)
    {
        return PeChunk {chunk->offset, static_cast<size_t>(m_streamSize - chunk->offset)};
    }

    return *chunk;
}

Result<void> PeParser::ReadDebugDirectory(std::vector<uint8_t>& buffer, std::optional<size_t> maxSize) const
{
    auto rv = ReadDirectory(IMAGE_DIRECTORY_ENTRY_DEBUG, buffer, maxSize);
    if (!rv)
    {
        Log::Debug("Failed to read IMAGE_DIRECTORY_ENTRY_DEBUG [{}]", rv.error());
        return rv.error();
    }

    return Success<void>();
}

uint64_t PeParser::GetChecksumOffset() const
{
    if (!m_imageOptionalHeader)
    {
        return 0;
    }

    // The value of the checksum offset is the same for 32/64 bits but it is kind of "more true"
    if (std::holds_alternative<IMAGE_OPTIONAL_HEADER32>(*m_imageOptionalHeader))
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeaderFixedSize)
            + offsetof(IMAGE_OPTIONAL_HEADER32, CheckSum);
    }
    else
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeaderFixedSize)
            + offsetof(IMAGE_OPTIONAL_HEADER64, CheckSum);
    }
}

uint64_t PeParser::GetSecurityDirectoryOffset() const
{
    if (!m_imageOptionalHeader)
    {
        return 0;
    }

    if (std::holds_alternative<IMAGE_OPTIONAL_HEADER32>(*m_imageOptionalHeader))
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeaderFixedSize)
            + offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]);
    }
    else
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeaderFixedSize)
            + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]);
    }
}

uint64_t PeParser::GetSizeOfOptionalHeader() const
{
    if (!m_imageOptionalHeader)
    {
        return 0;
    }

    return std::visit([&](const auto& optionalHeader) { return optionalHeader.SizeOfHeaders; }, *m_imageOptionalHeader);
}

void PeParser::GetHashedChunks(PeChunks& chunks, std::error_code& ec) const
{
    chunks[0].offset = 0;
    chunks[0].length = GetChecksumOffset();

    chunks[1].offset = chunks[0].length + sizeof(IMAGE_OPTIONAL_HEADER32::CheckSum);
    chunks[1].length = GetSecurityDirectoryOffset() - chunks[1].offset;

    chunks[2].offset = GetSecurityDirectoryOffset() + sizeof(IMAGE_DATA_DIRECTORY);
    chunks[2].length = GetSizeOfOptionalHeader() - chunks[2].offset;

    IMAGE_DATA_DIRECTORY secdir = {0};
    if (HasSecurityDirectory())
    {
        secdir = GetImageDataDirectory(IMAGE_DIRECTORY_ENTRY_SECURITY, ec);
        if (ec)
        {
            Log::Debug("Failed to retrieve security directory [{}]", ec);
            return;
        }
    }

    const auto fileSize = m_streamSize;
    const auto offsetChunk3 = GetSizeOfOptionalHeader();
    const uint64_t availableSize = (fileSize > offsetChunk3) ? (fileSize - offsetChunk3) : 0;
    if (secdir.Size > availableSize)
    {
        Log::Debug(
            "Security directory size exceeds available space (secdir.Size: {}, available: {})",
            secdir.Size,
            availableSize);
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    chunks[3].offset = offsetChunk3;
    chunks[3].length = m_streamSize - chunks[3].offset - secdir.Size;
}

void PeParser::Hash(CryptoHashStreamAlgorithm algorithms, const PeChunks& chunks, PeHash& output, std::error_code& ec)
    const
{
    auto hashstream = std::make_shared<CryptoHashStream>();
    HRESULT hr = hashstream->OpenToWrite(algorithms, nullptr);
    if (FAILED(hr))
    {
        Log::Debug("Failed to open hash stream [{}]", SystemError(hr));
        return;
    }

    const auto written = ::CopyChunks(chunks, m_stream, *hashstream, ec);
    if (ec)
    {
        Log::Debug("Failed to hash chunks [{}]", ec);
        return;
    }

    // MS does zero padding for PEs that are not 8 modulo (often with catalogs)
    const uint8_t padding[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const auto alignment = m_streamSize % sizeof(padding);
    if (alignment != 0)
    {
        const auto paddingLength = sizeof(padding) - alignment;
        Log::Trace("Add {} padding with 0x00 to hash stream", paddingLength);

        WriteChunk(*hashstream, BufferView(padding, paddingLength), ec);
        if (ec)
        {
            Log::Debug("Failed to write padding into hash stream [{}]", ec);
            return;
        }
    }

    CBinaryBuffer hash;
    if (HasFlag(algorithms, CryptoHashStreamAlgorithm::MD5))
    {
        hr = hashstream->GetMD5(hash);
        if (FAILED(hr))
        {
            Log::Debug("Failed to get md5 [{}]", SystemError(hr));
        }
        else
        {
            std::copy(std::begin(hash), std::end(hash), std::begin(output.md5.emplace()));
        }
    }

    if (HasFlag(algorithms, CryptoHashStreamAlgorithm::SHA1))
    {
        hr = hashstream->GetSHA1(hash);
        if (FAILED(hr))
        {
            Log::Debug("Failed to get sha1 [{}]", SystemError(hr));
        }
        else
        {
            std::copy(std::begin(hash), std::end(hash), std::begin(output.sha1.emplace()));
        }
    }

    if (HasFlag(algorithms, CryptoHashStreamAlgorithm::SHA256))
    {
        hr = hashstream->GetSHA256(hash);
        if (FAILED(hr))
        {
            Log::Debug("Failed to get sha256 [{}]", SystemError(hr));
        }
        else
        {
            std::copy(std::begin(hash), std::end(hash), std::begin(output.sha256.emplace()));
        }
    }
}

void PeParser::GetAuthenticodeHash(CryptoHashStreamAlgorithm algorithms, PeHash& output, std::error_code& ec) const
{
    PeChunks chunks;
    GetHashedChunks(chunks, ec);
    if (ec)
    {
        Log::Debug("Failed to retrieve chunks [{}]", ec);
        return;
    }

    Hash(algorithms, chunks, output, ec);
    if (ec)
    {
        Log::Debug("Failed to hash pe file [{}]", ec);
        return;
    }
}

Result<PeParser::PeChunk> PeParser::GetResourceDataChunk(
    std::variant<WORD, std::wstring_view> type,
    std::variant<WORD, std::wstring_view> name,
    std::optional<WORD> lang) const
{
    auto resourceDirChunk = GetResourceDirectoryChunk();
    if (!resourceDirChunk)
    {
        Log::Debug("Failed to retrieve resource directory chunk [{}]", resourceDirChunk.error());
        return resourceDirChunk.error();
    }

    // Make a substream for resource directory parsing and bounds checking (resource data can be outside of it)
    SubStream resourceDirStream(m_stream, resourceDirChunk->offset, resourceDirChunk->length);
    HRESULT hr = resourceDirStream.Open();
    if (FAILED(hr))
    {
        Log::Debug("Failed to open resource directory chunk stream [{}]", SystemError(hr));
        return SystemError(hr);
    }

    auto resourceEntry = ::GetResourceDataEntry(resourceDirStream, type, name, lang);
    if (!resourceEntry)
    {
        Log::Debug("Failed to get resource data entry [{}]", resourceEntry.error());
        return resourceEntry.error();
    }

    const auto fileOffsetToData = ImageRvaToFileOffset(resourceEntry->OffsetToData);
    if (!fileOffsetToData)
    {
        Log::Debug("Failed to convert resource data RVA to file offset [{}]", fileOffsetToData.error());
        return fileOffsetToData.error();
    }

    PeChunk chunk {*fileOffsetToData, resourceEntry->Size};
    if (chunk.offset >= m_streamSize)
    {
        Log::Debug("Invalid resource data offset (value: {:#x}, stream size: {})", chunk.offset, m_streamSize);
        return std::errc::bad_message;
    }

    if (chunk.offset + chunk.length > m_streamSize)
    {
        // In the wild some pe files have invalid resource size that overlap the file size but data is actually present
        Log::Debug("Invalid resource data size (value: {}, stream size: {})", chunk.length, m_streamSize);
        chunk.length = m_streamSize - chunk.offset;
    }

    return chunk;
}

// TODO: not thoroughly tested but useful for Forge and ExtractResource
Result<std::vector<uint8_t>> PeParser::GetResource(
    std::variant<WORD, std::wstring_view> type,
    std::variant<WORD, std::wstring_view> name,
    std::optional<WORD> lang,
    std::optional<size_t> maxSize) const
{
    auto chunk = GetResourceDataChunk(type, name, lang);
    if (!chunk)
    {
        Log::Debug("Failed to get resource offset [{}]", chunk.error());
        return chunk.error();
    }

    if (maxSize && chunk->length > *maxSize)
    {
        Log::Debug(
            "Invalid resource data size: exceed hard limit (expected: >{}, actual: {})", *maxSize, chunk->length);
        return std::errc::bad_message;
    }

    std::error_code ec;
    std::vector<uint8_t> data;
    ReadChunkAt(m_stream, chunk->offset, chunk->length, data, ec);
    if (ec)
    {
        Log::Debug("Failed to read resource data [{}]", ec);
        return ec;
    }

    return data;
}

// TODO: not thoroughly tested but useful for Forge
Result<PeParser::ResourceEntryTree> PeParser::FindResources(
    std::variant<std::monostate, WORD, std::wstring_view> type,
    std::variant<std::monostate, WORD, std::wstring_view> name,
    std::variant<std::monostate, WORD> langId,
    std::optional<size_t> maxItemCount) const
{
    auto resourceChunk = GetResourceDirectoryChunk();
    if (!resourceChunk)
    {
        Log::Debug("Failed to retrieve resource directory chunk [{}]", resourceChunk.error());
        return resourceChunk.error();
    }

    // Make a substream for resource directory parsing and bounds checking (resource data can be outside of it)
    SubStream resourceDirStream(m_stream, resourceChunk->offset, resourceChunk->length);
    HRESULT hr = resourceDirStream.Open();
    if (FAILED(hr))
    {
        Log::Debug("Failed to open resource directory chunk stream [{}]", SystemError(hr));
        return SystemError(hr);
    }

    size_t itemCount = 0;
    ResourceEntryTree resources;

    auto typeDirectory = ParseResourceDirectory(resourceDirStream, 0);
    if (!typeDirectory)
    {
        return typeDirectory.error();
    }

    for (const auto& typeEntry : typeDirectory->entries)
    {
        auto typeNameOrId = GetResourceEntryNameOrId(resourceDirStream, typeEntry);
        if (!typeNameOrId)
        {
            Log::Debug("Failed to parse resource type name [{}]", typeNameOrId.error());
            continue;
        }

        if (IsFiltered(type, *typeNameOrId))
        {
            continue;
        }

        auto& typeView = resources.types.emplace_back(TypeEntry {*typeNameOrId, typeEntry});

        if (!typeEntry.DataIsDirectory)
        {
            Log::Debug("Type entry is not a directory");
            continue;
        }

        auto nameDirectory = ParseResourceDirectory(resourceDirStream, typeEntry.OffsetToDirectory);
        if (!nameDirectory)
        {
            Log::Debug("Failed to parse entries for resource [{}]", nameDirectory.error());
            continue;
        }

        for (auto& nameEntry : nameDirectory->entries)
        {
            auto nameOrId = GetResourceEntryNameOrId(resourceDirStream, nameEntry);
            if (!nameOrId)
            {
                Log::Debug("Failed to parse resource name [{}]", nameOrId.error());
                continue;
            }

            if (IsFiltered(name, *nameOrId))
            {
                continue;
            }

            auto& nameView = typeView.names.emplace_back(NameEntry {*nameOrId, nameEntry});

            if (!nameEntry.DataIsDirectory)
            {
                Log::Debug("Name entry is not a directory");
                continue;
            }

            auto langDirectory = ParseResourceDirectory(resourceDirStream, nameEntry.OffsetToDirectory);
            if (!langDirectory)
            {
                Log::Debug("Failed to parse entries for resource [{}]", langDirectory.error());
                continue;
            }

            for (auto& langEntry : langDirectory->entries)
            {
                const auto id = std::get_if<WORD>(&langId);
                if (id != nullptr && *id != langEntry.Id)
                {
                    continue;
                }

                if (langEntry.DataIsDirectory)
                {
                    Log::Debug("Lang entry is a directory, expected data");
                    continue;
                }

                auto dataEntry = GetResourceDataEntry(resourceDirStream, langEntry.OffsetToData);
                if (!dataEntry)
                {
                    Log::Debug("Failed to parse resource data entry [{}]", dataEntry.error());
                    continue;
                }

                nameView.langs.emplace_back(LangEntry {langEntry.Id, langEntry, *dataEntry});

                ++itemCount;
                if (maxItemCount && itemCount >= *maxItemCount)
                {
                    return resources;
                }
            }
        }
    }

    return resources;
}

}  // namespace Orc
