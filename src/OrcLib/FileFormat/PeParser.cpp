//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
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
        processed += ::CopyChunk(input, chunk, output, ec);
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
        Log::Debug("Failed to read IMAGE_DOS_HEADER [{}]", ec);
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
            Log::Debug("Invalid IMAGE_DOS_HEADER magic code (value: {:#x})", header.e_magic);
            return;
    }
}

void ParseImageNtHeader(
    ByteStream& stream,
    const IMAGE_DOS_HEADER& imageDosHeader,
    PeParser::ImageNtHeader& imageNtHeader,
    std::optional<IMAGE_OPTIONAL_HEADER32>& optionalHeaders32,
    std::optional<IMAGE_OPTIONAL_HEADER64>& optionalHeaders64,
    std::error_code& ec)
{
    ReadItemAt(stream, imageDosHeader.e_lfanew, imageNtHeader, ec);
    if (ec)
    {
        Log::Debug("Failed to read IMAGE_NT_HEADER [{}]", ec);
        return;
    }

    if (imageNtHeader.Signature != IMAGE_NT_SIGNATURE)
    {
        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug("Invalid IMAGE_NT_SIGNATURE (value: {:#x})", imageNtHeader.Signature);
        return;
    }

    switch (imageNtHeader.FileHeader.Machine)
    {
        case IMAGE_FILE_MACHINE_I386:
        case IMAGE_FILE_MACHINE_ARMNT:
            optionalHeaders32 = IMAGE_OPTIONAL_HEADER32 {0};
            ReadItem(stream, optionalHeaders32.value(), ec);
            if (ec)
            {
                Log::Debug("Failed to read IMAGE_OPTIONAL_HEADER32 [{}]", ec);
                return;
            }

            if (optionalHeaders32->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            {
                Log::Debug("Invalid IMAGE_NT_OPTIONAL_HDR32_MAGIC (value: {})", optionalHeaders32->Magic);
                return;
            }

            break;
        case IMAGE_FILE_MACHINE_AMD64:
        case IMAGE_FILE_MACHINE_IA64:
        case IMAGE_FILE_MACHINE_ARM64:
            optionalHeaders64 = IMAGE_OPTIONAL_HEADER64 {0};
            ReadItem(stream, optionalHeaders64.value(), ec);
            if (ec)
            {
                Log::Debug("Failed to read IMAGE_OPTIONAL_HEADER64 [{}]", ec);
                return;
            }

            if (optionalHeaders64->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                Log::Debug("Invalid IMAGE_NT_OPTIONAL_HDR64_MAGIC (value: {})", optionalHeaders64->Magic);
                return;
            }

            break;
        default:
            Log::Debug("Unsupported machine (value: {})", imageNtHeader.FileHeader.Machine);
            ec = std::make_error_code(std::errc::function_not_supported);
            return;
    }
}

// BEWARE: this function expects the stream to be at the right file position (just after optional headers)
void ParseImageSections(
    ByteStream& stream,
    const PeParser::ImageNtHeader& imageNtHeaders,
    PeParser::SectionHeaders& imageSectionHeader,
    std::error_code& ec)
{
    const auto kMaxSectionCount = 64;

    if (imageNtHeaders.FileHeader.NumberOfSections >= kMaxSectionCount)
    {
        // Log as error as this could be malicious behavior
        Log::Error("Invalid PE with too many section (value: {})", imageNtHeaders.FileHeader.NumberOfSections);
        ec = std::make_error_code(std::errc::bad_message);
        return;
    }

    imageSectionHeader.resize(imageNtHeaders.FileHeader.NumberOfSections);
    ReadChunk(stream, imageSectionHeader, ec);
    if (ec)
    {
        Log::Debug("Failed to read sections [{}]", ec);
        return;
    }
}

}  // namespace

namespace Orc {

PeParser::PeParser(std::shared_ptr<ByteStream> stream, std::error_code& ec)
    : m_stream(std::move(stream))
{
    ::ParseImageDosHeader(*m_stream, m_imageDosHeader, ec);
    if (ec)
    {
        return;
    }

    ::ParseImageNtHeader(
        *m_stream, m_imageDosHeader, m_imageNtHeader, m_imageOptionalHeaders32, m_imageOptionalHeaders64, ec);
    if (ec)
    {
        return;
    }

    ::ParseImageSections(*m_stream, m_imageNtHeader, m_imageSectionsHeaders, ec);
    if (ec)
    {
        return;
    }
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
    const auto kMaxDirectoryIndex = 14;  // 'IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR'
    if (index > kMaxDirectoryIndex)
    {
        return false;
    }

    IMAGE_DATA_DIRECTORY data;
    if (m_imageOptionalHeaders32)
    {
        data = m_imageOptionalHeaders32->DataDirectory[index];
    }
    else if (m_imageOptionalHeaders64)
    {
        data = m_imageOptionalHeaders64->DataDirectory[index];
    }
    else
    {
        return false;
    }

    if (data.Size == 0)
    {
        return false;
    }

    return true;
}

IMAGE_DATA_DIRECTORY PeParser::GetImageDataDirectory(uint8_t index, std::error_code& ec) const
{
    const auto kMaxDirectoryIndex = 14;  // 'IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR'
    if (index > kMaxDirectoryIndex)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        Log::Debug("Invalid directory (index: {}, expected: >{})", index, kMaxDirectoryIndex);
        return {};
    }

    IMAGE_DATA_DIRECTORY data;
    if (m_imageOptionalHeaders32)
    {
        data = m_imageOptionalHeaders32->DataDirectory[index];
    }
    else if (m_imageOptionalHeaders64)
    {
        data = m_imageOptionalHeaders64->DataDirectory[index];
    }
    else
    {
        ec = std::make_error_code(std::errc::bad_message);
        return {};
    }

    if (data.Size == 0)
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }

    return data;
}

void PeParser::ReadDirectory(uint8_t index, std::vector<uint8_t>& buffer, std::error_code& ec) const
{
    const auto directory = GetImageDataDirectory(index, ec);
    if (ec)
    {
        Log::Debug("Failed to retrieve directory (index: {}) [{}]", index, ec);
        return;
    }

    const auto kMaxSecurityDirectorySize = 16 << 20;  // '<< 20' <=> MB
    if (directory.Size > kMaxSecurityDirectorySize)
    {
        Log::Debug(
            "Invalid directory size (index: {}, value: {}, expected: >{}",
            index,
            directory.Size,
            kMaxSecurityDirectorySize);

        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    buffer.resize(directory.Size);
    ReadChunkAt(*m_stream, directory.VirtualAddress, buffer, ec);
    if (ec)
    {
        Log::Debug("Failed to read directory (index: {}) [{}]", index, ec);
        return;
    }
}

void PeParser::ReadSecurityDirectory(std::vector<uint8_t>& buffer, std::error_code& ec) const
{
    ReadDirectory(IMAGE_DIRECTORY_ENTRY_SECURITY, buffer, ec);
    if (ec)
    {
        Log::Debug("Failed to read IMAGE_DIRECTORY_ENTRY_SECURITY [{}]", ec);
        return;
    }
}

void PeParser::ReadDebugDirectory(std::vector<uint8_t>& buffer, std::error_code& ec) const
{
    ReadDirectory(IMAGE_DIRECTORY_ENTRY_DEBUG, buffer, ec);
    if (ec)
    {
        Log::Debug("Failed to read IMAGE_DIRECTORY_ENTRY_DEBUG [{}]", ec);
        return;
    }
}

uint64_t PeParser::GetChecksumOffset() const
{
    // The value of the checksum offset is the same for 32/64 bits but it is kind of "more true
    if (m_imageOptionalHeaders32)
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeader) + offsetof(struct IMAGE_OPTIONAL_HEADER32, CheckSum);
    }
    else
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeader) + offsetof(struct IMAGE_OPTIONAL_HEADER64, CheckSum);
    }
}

uint64_t PeParser::GetSecurityDirectoryOffset() const
{
    if (m_imageOptionalHeaders32)
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeader)
            + offsetof(struct IMAGE_OPTIONAL_HEADER32, DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]);
    }
    else
    {
        return m_imageDosHeader.e_lfanew + sizeof(ImageNtHeader)
            + offsetof(struct IMAGE_OPTIONAL_HEADER64, DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]);
    }
}

uint64_t PeParser::GetSizeOfOptionalHeaders() const
{
    if (m_imageOptionalHeaders32)
    {
        return m_imageOptionalHeaders32->SizeOfHeaders;
    }
    else
    {
        return m_imageOptionalHeaders64->SizeOfHeaders;
    }
}

void PeParser::GetHashedChunks(PeChunks& chunks, std::error_code& ec) const
{
    chunks[0].offset = 0;
    chunks[0].length = GetChecksumOffset();

    chunks[1].offset = chunks[0].length + sizeof(IMAGE_OPTIONAL_HEADER32::CheckSum);
    chunks[1].length = GetSecurityDirectoryOffset() - chunks[1].offset;

    chunks[2].offset = GetSecurityDirectoryOffset() + sizeof(IMAGE_DATA_DIRECTORY);
    chunks[2].length = GetSizeOfOptionalHeaders() - chunks[2].offset;

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

    chunks[3].offset = GetSizeOfOptionalHeaders();
    chunks[3].length = m_stream->GetSize() - chunks[3].offset - secdir.Size;
}

void PeParser::Hash(CryptoHashStreamAlgorithm algorithms, const PeChunks& chunks, PeHash& output, std::error_code& ec)
    const
{
    // TODO: only process the expected algorithm
    auto hashstream = std::make_shared<CryptoHashStream>();
    HRESULT hr = hashstream->OpenToWrite(algorithms, nullptr);
    if (FAILED(hr))
    {
        Log::Debug("Failed to open hash stream [{}]", SystemError(hr));
        return;
    }

    const auto written = ::CopyChunks(chunks, *m_stream, *hashstream, ec);
    if (ec)
    {
        Log::Debug("Failed to hash chunks [{}]", ec);
        return;
    }

    // MS does zero padding for PEs that are not 8 modulo (often with catalogs)
    const uint8_t padding[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const auto alignment = m_stream->GetSize() % sizeof(padding);
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
        hashstream->GetSHA1(hash);
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
        hashstream->GetSHA256(hash);
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
