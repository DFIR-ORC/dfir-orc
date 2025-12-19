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
    PeParser::OptionalHeader& imageOptionalHeader,
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

const PeParser::OptionalHeader& PeParser::ImageOptionalHeader() const
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

    IMAGE_DATA_DIRECTORY data;
    std::visit([&](const auto& optionalHeader) { data = optionalHeader.DataDirectory[index]; }, m_imageOptionalHeader);

    if (data.Size == 0)
    {
        return false;
    }

    return true;
}

IMAGE_DATA_DIRECTORY PeParser::GetImageDataDirectory(uint8_t index, std::error_code& ec) const
{
    if (index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        Log::Debug("Invalid directory (index: {}, expected: <{})", index, IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
        return {};
    }

    IMAGE_DATA_DIRECTORY data;
    std::visit([&](const auto& optionalHeader) { data = optionalHeader.DataDirectory[index]; }, m_imageOptionalHeader);

    if (data.Size == 0)
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }

    return data;
}

Result<void> PeParser::ReadDirectory(uint8_t index, std::vector<uint8_t>& buffer, std::optional<size_t> maxSize) const
{
    std::error_code ec;

    const auto directory = GetImageDataDirectory(index, ec);
    if (ec)
    {
        Log::Debug("Failed to retrieve directory (index: {}) [{}]", index, ec);
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

    buffer.resize(directory.Size);
    ReadChunkAt(m_stream, fileOffset, buffer, ec);
    if (ec)
    {
        Log::Debug("Failed to read directory (index: {}) [{}]", index, ec);
        return ec;
    }

    return Orc::Success<void>();
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
    // The value of the checksum offset is the same for 32/64 bits but it is kind of "more true"
    if (std::holds_alternative<IMAGE_OPTIONAL_HEADER32>(m_imageOptionalHeader))
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
    if (std::holds_alternative<IMAGE_OPTIONAL_HEADER32>(m_imageOptionalHeader))
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
    return std::visit([&](const auto& optionalHeader) { return optionalHeader.SizeOfHeaders; }, m_imageOptionalHeader);
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

    chunks[3].offset = GetSizeOfOptionalHeader();
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

}  // namespace Orc
