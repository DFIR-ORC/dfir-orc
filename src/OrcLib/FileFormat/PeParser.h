//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "CryptoHashStreamAlgorithm.h"

#include <filesystem>
#include <vector>
#include <system_error>

namespace Orc {

class PeParser
{
public:
#pragma pack(push, 1)
    // Like IMAGE_NT_HEADERS32/IMAGE_NT_HEADERS64 but keeps only fixed fields size
    struct ImageNtHeader
    {
        DWORD Signature;
        IMAGE_FILE_HEADER FileHeader;
    };
#pragma pack(pop)

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
    using SectionHeaders = fmt::basic_memory_buffer<IMAGE_SECTION_HEADER, 16>;

    PeParser(std::shared_ptr<ByteStream> stream, std::error_code& ec);

    bool HasSecurityDirectory() const;
    void ReadSecurityDirectory(std::vector<uint8_t>& buffer, std::error_code& ec) const;
    void GetAuthenticodeHash(CryptoHashStreamAlgorithm algorithms, PeHash& output, std::error_code& ec) const;

private:
    bool HasImageDataDirectory(uint8_t index) const;
    IMAGE_DATA_DIRECTORY GetImageDataDirectory(uint8_t index, std::error_code& ec) const;
    void ReadDirectory(uint8_t index, std::vector<uint8_t>& buffer, std::error_code& ec) const;

    uint64_t GetSizeOfOptionalHeaders() const;
    uint64_t GetSecurityDirectoryOffset() const;
    uint64_t GetChecksumOffset() const;

    void GetHashedChunks(PeChunks& chunks, std::error_code& ec) const;
    void Hash(CryptoHashStreamAlgorithm algorithms, const PeChunks& chunks, PeHash& output, std::error_code& ec) const;

private:
    mutable std::shared_ptr<ByteStream> m_stream;
    IMAGE_DOS_HEADER m_imageDosHeader;
    ImageNtHeader m_imageNtHeader;
    std::optional<IMAGE_OPTIONAL_HEADER32> m_imageOptionalHeaders32;
    std::optional<IMAGE_OPTIONAL_HEADER64> m_imageOptionalHeaders64;
    SectionHeaders m_imageSectionsHeaders;
};

}  // namespace Orc
