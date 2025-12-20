//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>
#include <vector>
#include <system_error>
#include <variant>

#include "CryptoHashStreamAlgorithm.h"

namespace Orc {

class PeParser
{
public:
    struct PeChunk
    {
        uint64_t offset;
        size_t length;
    };

    struct PeHash
    {
        std::optional<std::array<uint8_t, 16>> md5;
        std::optional<std::array<uint8_t, 20>> sha1;
        std::optional<std::array<uint8_t, 32>> sha256;
    };

    using PeChunks = std::array<PeChunk, 4>;
    using OptionalHeader = std::variant<IMAGE_OPTIONAL_HEADER32, IMAGE_OPTIONAL_HEADER64>;
    using SectionHeaders = fmt::basic_memory_buffer<IMAGE_SECTION_HEADER, 16>;

    PeParser(ByteStream& stream, std::error_code& ec);

    const IMAGE_DOS_HEADER& ImageDosHeader() const;
    const std::optional<IMAGE_FILE_HEADER>& ImageFileHeader() const;
    const OptionalHeader& ImageOptionalHeader() const;

    bool HasResourceDirectory() const;
    Result<void> ReadResourceDirectory(std::vector<uint8_t>& buffer, std::optional<size_t> maxLen = {}) const;

    bool HasSecurityDirectory() const;
    Result<void> ReadSecurityDirectory(std::vector<uint8_t>& buffer, std::optional<size_t> maxLen = 1048576 * 32) const;

    bool HasDebugDirectory() const;
    Result<void> ReadDebugDirectory(std::vector<uint8_t>& buffer, std::optional<size_t> maxLen = {}) const;
    void GetAuthenticodeHash(CryptoHashStreamAlgorithm algorithms, PeHash& output, std::error_code& ec) const;

private:
    bool HasImageDataDirectory(uint8_t index) const;
    IMAGE_DATA_DIRECTORY GetImageDataDirectory(uint8_t index, std::error_code& ec) const;

    Result<void> ReadDirectory(uint8_t index, std::vector<uint8_t>& buffer, std::optional<size_t> maxSize = 0) const;
    Result<uint64_t> ImageRvaToFileOffset(uint32_t rva, std::optional<size_t> chunkSizeForValidation = {}) const;

    uint64_t GetSizeOfOptionalHeader() const;
    uint64_t GetSecurityDirectoryOffset() const;
    uint64_t GetChecksumOffset() const;

    void GetHashedChunks(PeChunks& chunks, std::error_code& ec) const;
    void Hash(CryptoHashStreamAlgorithm algorithms, const PeChunks& chunks, PeHash& output, std::error_code& ec) const;

private:
    ByteStream& m_stream;
    uint64_t m_streamSize;  // Cached stream size because underlying stream may SetFilePointer to get it
    IMAGE_DOS_HEADER m_imageDosHeader;
    std::optional<IMAGE_FILE_HEADER> m_imageFileHeader;
    OptionalHeader m_imageOptionalHeader;
    SectionHeaders m_imageSectionsHeaders;
};

}  // namespace Orc
