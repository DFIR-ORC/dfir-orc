//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "PeVersionInfo.h"

#include <boost/algorithm/string.hpp>

#include "PeParser.h"
#include "Utils/BufferView.h"
#include "ByteStreamHelpers.h"
#include "Text/Hex.h"

//
// STRUCTURE                           wType VALUE    DESCRIPTION
//---------------------------------------------------------------------------
// VS_VERSIONINFO (root)               0 (Binary)     Value is VS_FIXEDFILEINFO struct
// StringFileInfo                      1 (Text)       Contains text strings
// StringTable                         1 (Text)       Contains text key-value pairs
// String (individual entry)           1 (Text)       Both key and value are text
// VarFileInfo                         1 (Text)       Contains Var structures
// Var (Translation)                   0 (Binary)     Value is array of DWORD (lang/codepage)
//
//
// EXAMPLE LAYOUT:
//---------------------------------------------------------------------------
// VS_VERSIONINFO                  wType=0  wValueLength=52 (VS_FIXEDFILEINFO size)
//     Value: VS_FIXEDFILEINFO (binary structure)
//
//     StringFileInfo              wType=1  wValueLength=0 (no direct value)
//         StringTable "040904b0"  wType=1  wValueLength=0
//             String              wType=1  wValueLength=varies
//                 Key: "CompanyName"       (text)
//                 Value: "Microsoft Corp"  (text)
//             String              wType=1  wValueLength=varies
//                 Key: "FileVersion"       (text)
//                 Value: "10.0.19041.1"    (text)
//
//     VarFileInfo                 wType=1  wValueLength=0
//         Var "Translation"       wType=0  wValueLength=4
//             Value: 0x040904B0 (DWORD: 0x0409=English, 0x04B0=Unicode)

using namespace Orc;

namespace {

constexpr uint32_t FIXED_FILE_INFO_SIGNATURE = 0xfeef04bd;
constexpr uint16_t ENGLISH_LANG_ID = 0x409;
constexpr uint16_t UTF16_CODEPAGE = 0x4b0;
constexpr uint16_t LATIN1_CODEPAGE = 0x4e4;

size_t AlignToDword(size_t offset)
{
    return (offset + 3) & ~3;
}

#pragma pack(push, 1)
struct VS_VERSION_HEADER
{
    WORD wLength;  // Length of this structure
    WORD wValueLength;  // Length of Value member
    WORD wType;  // 1=Text, 0=Binary
    WCHAR szKey[1];  // Variable length null-terminated string
};
#pragma pack(pop)

struct BlockView
{
    std::wstring_view key;
    std::variant<std::wstring_view, BufferView> value;
    std::vector<BufferView> rawChilds;
};

struct BlockViewTree
{
    BlockView block;
    std::vector<BlockViewTree> childNodes;
};

Result<VS_FIXEDFILEINFO> ParseFixedFileInfo(const BlockView& block)
{
    auto fixedFileInfoBuffer = std::get_if<BufferView>(&block.value);
    if (!fixedFileInfoBuffer)
    {
        Log::Debug("Failed VersionInfo: missing VS_FIXEDFILEINFO");
        return std::errc::bad_message;
    }

    if (fixedFileInfoBuffer->size() < sizeof(VS_FIXEDFILEINFO))
    {
        Log::Debug("Failed VersionInfo: truncated VS_FIXEDFILEINFO");
        return std::errc::bad_message;
    }

    auto fixedFileInfo = *reinterpret_cast<const VS_FIXEDFILEINFO*>(fixedFileInfoBuffer->data());
    if (fixedFileInfo.dwSignature != FIXED_FILE_INFO_SIGNATURE)
    {
        Log::Debug("Failed VersionInfo: invalid VS_FIXEDFILEINFO signature");
        return std::errc::bad_message;
    }

    return fixedFileInfo;
}

Result<std::wstring_view> ExtractKey(BufferView block, const VS_VERSION_HEADER* header)
{
    const size_t maxKeyChars = (header->wLength - offsetof(VS_VERSION_HEADER, szKey)) / sizeof(wchar_t);
    std::wstring_view headerEnd(header->szKey, maxKeyChars);

    auto keyEnd = headerEnd.find_first_of(L'\0');
    if (keyEnd == std::wstring_view::npos)
    {
        Log::Debug("Invalid version block: key missing null terminator");
        return std::errc::bad_message;
    }

    return std::wstring_view(header->szKey, keyEnd);
}

Result<std::wstring_view> ExtractTextValue(BufferView block, const VS_VERSION_HEADER* header, size_t valueOffset)
{
    // Accept invalid wValueLength that exceed block size, but clamp to block size
    const size_t cbValue = std::min(block.size() - valueOffset, header->wValueLength * sizeof(wchar_t));
    if (!IsValidOffset(block, valueOffset, cbValue))
    {
        Log::Debug("Invalid text value: offset or size out of bounds");
        return std::errc::bad_message;
    }

    std::wstring_view valueView(reinterpret_cast<const WCHAR*>(block.data() + valueOffset), header->wValueLength);
    auto nullPos = valueView.find_first_of(L'\0');
    size_t length = (nullPos == std::wstring_view::npos) ? header->wValueLength : nullPos;
    length = std::min(length, valueView.size());
    return valueView.substr(0, length);
}

Result<BufferView> ExtractBinaryValue(BufferView block, const VS_VERSION_HEADER* header, size_t valueOffset)
{
    // Contrary to text values, binary values must fit entirely in the block (no clamping seen in the wild)
    if (!IsValidOffset(block, valueOffset, header->wValueLength))
    {
        Log::Debug("Invalid binary value: offset or size out of bounds");
        return std::errc::bad_message;
    }

    return BufferView(block.data() + valueOffset, header->wValueLength);
}

Result<std::variant<std::wstring_view, BufferView>>
ExtractValue(BufferView block, const VS_VERSION_HEADER* header, size_t valueOffset)
{
    if (header->wType == 1)
    {
        if (header->wValueLength == 0)
        {
            return std::wstring_view();
        }

        auto textValue = ExtractTextValue(block, header, valueOffset);
        if (!textValue)
        {
            return textValue.error();
        }

        return *textValue;
    }
    else if (header->wType == 0)
    {
        if (header->wValueLength == 0)
        {
            return BufferView();
        }

        auto binaryValue = ExtractBinaryValue(block, header, valueOffset);
        if (!binaryValue)
        {
            return binaryValue.error();
        }

        return *binaryValue;
    }

    Log::Debug("Invalid wType value: {}", header->wType);
    return std::errc::bad_message;
}

std::vector<BufferView> ExtractChildren(BufferView block, const VS_VERSION_HEADER* header, size_t startOffset)
{
    std::vector<BufferView> children;
    auto currentOffset = startOffset;

    while (currentOffset < header->wLength)
    {
        if (!IsValidOffset(block, currentOffset, sizeof(VS_VERSION_HEADER)))
        {
            break;
        }

        auto childHeader = reinterpret_cast<const VS_VERSION_HEADER*>(block.data() + currentOffset);

        if (childHeader->wLength == 0)
        {
            Log::Debug("Invalid version block header: child header length is null");
            break;
        }

        if (!IsValidOffset(block, currentOffset, childHeader->wLength))
        {
            Log::Debug("Invalid version block header: child overlap");
            break;
        }

        if (currentOffset + childHeader->wLength > header->wLength)
        {
            Log::Debug("Invalid version block header: child overlap (exceeds parent length)");
            break;
        }

        children.emplace_back(block.data() + currentOffset, childHeader->wLength);
        currentOffset = AlignToDword(currentOffset + childHeader->wLength);
    }

    return children;
}

Result<BlockView> Parse(BufferView block)
{
    if (!IsValidOffset(block, 0, sizeof(VS_VERSION_HEADER)))
    {
        Log::Debug("Invalid version block header: VS_VERSION_HEADER does not fit");
        return std::errc::bad_message;
    }

    auto header = reinterpret_cast<const VS_VERSION_HEADER*>(block.data());
    if (!IsValidOffset(block, 0, header->wLength) || header->wLength == 0)
    {
        Log::Debug("Invalid version block header: invalid length");
        return std::errc::bad_message;
    }

    auto key = ExtractKey(block, header);
    if (!key)
    {
        return key.error();
    }

    const auto offset =
        reinterpret_cast<const uint8_t*>(key->data()) + (key->size() + sizeof('\0')) * sizeof(wchar_t) - block.data();
    const auto valueOffset = AlignToDword(offset);

#ifdef _DEBUG
    Log::Debug(
        L"Parse: key='{}', key.size()={}, offset={}, valueOffset={}, block.size()={}, header->wLength={}, "
        L"header->wValueLength={}, header->wType={}",
        *key,
        key->size(),
        offset,
        valueOffset,
        block.size(),
        header->wLength,
        header->wValueLength,
        header->wType);
#endif

    auto value = ExtractValue(block, header, valueOffset);
    if (!value)
    {
        return value.error();
    }

    const size_t cbValue = static_cast<size_t>(header->wValueLength) * (header->wType == 1 ? sizeof(wchar_t) : 1);
    auto children = ExtractChildren(block, header, AlignToDword(valueOffset + cbValue));

    return BlockView {*key, *value, std::move(children)};
}

Result<void> BuildBlockViewTree(BlockViewTree& node, BufferView blockBuffer, std::optional<uint32_t> maxDepth = {})
{
    if (maxDepth.has_value())
    {
        if (*maxDepth == 0)
        {
            Log::Debug("Maximum recursion depth reached");
            return std::errc::bad_message;
        }
        else
        {
            maxDepth.value()--;
        }
    }

    auto block = Parse(blockBuffer);
    if (!block)
    {
        return block.error();
    }

    node.block = std::move(*block);
    for (auto& childBuffer : node.block.rawChilds)
    {
        BlockViewTree childNode;
        auto rv = BuildBlockViewTree(childNode, childBuffer, maxDepth);
        if (rv.has_error())
        {
            continue;
        }

        node.childNodes.emplace_back(std::move(childNode));
    }

    return Orc::Success<void>();
}

bool IsPreferredLanguage(uint16_t lang, uint16_t codepage)
{
    const bool isEnglishOrNeutral = (lang == ENGLISH_LANG_ID || lang == 0);
    const bool isValidCodepage = (codepage == UTF16_CODEPAGE || codepage == LATIN1_CODEPAGE || codepage == 0);
    return isEnglishOrNeutral && isValidCodepage;
}

const BlockViewTree* FindPreferredStringTable(const std::vector<BlockViewTree>& tables)
{
    for (const auto& node : tables)
    {
        auto langcp = Text::FromHexToLittleEndian<uint32_t>(node.block.key);
        if (!langcp)
        {
            continue;
        }

        const auto lang = HIWORD(*langcp);
        const auto codepage = LOWORD(*langcp);
        if (IsPreferredLanguage(lang, codepage))
        {
            return &node;
        }
    }

    return tables.empty() ? nullptr : &tables[0];
}

void ExtractStringTable(const BlockViewTree* stringTable, std::map<std::wstring, std::wstring>& output)
{
    if (!stringTable)
    {
        return;
    }

    for (const auto& item : stringTable->childNodes)
    {
        auto value = std::get_if<std::wstring_view>(&item.block.value);
        if (!value)
        {
            Log::Debug(L"StringTable entry is not text: {}", item.block.key);
            continue;
        }

        output.emplace(item.block.key, *value);
    }
}

Result<void> ParseVersionInfo(
    BufferView versionInfo,
    std::optional<VS_FIXEDFILEINFO>& fixedFileInfo,
    std::map<std::wstring, std::wstring>& stringTable)
{
    std::error_code ec;

    BlockViewTree root;
    auto rv = BuildBlockViewTree(root, versionInfo);
    if (!rv)
    {
        return rv.error();
    }

    if (root.block.key != L"VS_VERSION_INFO")
    {
        Log::Debug("Failed VersionInfo: missing 'VS_VERSION_INFO'");
        return std::make_error_code(std::errc::bad_message);
    }

    if (auto entry = ParseFixedFileInfo(root.block))
    {
        fixedFileInfo = *entry;
    }

    for (auto& childNode : root.childNodes)
    {
        if (childNode.block.key != L"StringFileInfo")
        {
            continue;
        }

        const auto& stringFileInfo = childNode;
        auto rawStringTable = FindPreferredStringTable(stringFileInfo.childNodes);
        ExtractStringTable(rawStringTable, stringTable);
        break;
    }

    return Orc::Success<void>();
}

}  // namespace

namespace Orc {

PeVersionInfo::PeVersionInfo(ByteStream& stream, std::error_code& ec)
{
    PeParser parser(stream, ec);
    if (ec)
    {
        return;
    }

    // IDEA: could look firstly for lang #0 (neutral) or lang #1033 (en-US) instead of blindly taking the first
    auto versionInfo = parser.GetResource(reinterpret_cast<WORD>(RT_VERSION), static_cast<WORD>(VS_VERSION_INFO));
    if (!versionInfo)
    {
        ec = versionInfo.error();
        return;
    }

    auto rv = ::ParseVersionInfo(*versionInfo, m_fixedFileInfo, m_stringFileInfo);
    if (!rv)
    {
        ec = rv.error();
        return;
    }
}

PeVersionInfo::PeVersionInfo(ByteStream& stream, const PeParser::PeChunk& versionInfo, std::error_code& ec)
{
    std::vector<uint8_t> buffer;

    try
    {
        buffer.resize(versionInfo.length);
    }
    catch (...)
    {
        Log::Debug("Failed to read version info: not enough memory (size: {})", versionInfo.length);
        ec = std::make_error_code(std::errc::not_enough_memory);
        return;
    }

    ReadChunkAt(stream, versionInfo.offset, buffer, ec);
    if (ec)
    {
        return;
    }

    auto rv = ::ParseVersionInfo(buffer, m_fixedFileInfo, m_stringFileInfo);
    if (!rv)
    {
        ec = rv.error();
        return;
    }
}

PeVersionInfo::PeVersionInfo(BufferView versionInfo, std::error_code& ec)
{
    auto rv = ::ParseVersionInfo(versionInfo, m_fixedFileInfo, m_stringFileInfo);
    if (!rv)
    {
        ec = rv.error();
        return;
    }
}

const std::optional<VS_FIXEDFILEINFO>& PeVersionInfo::FixedFileInfo() const
{
    return m_fixedFileInfo;
}

const std::map<std::wstring, std::wstring>& PeVersionInfo::StringFileInfo() const
{
    return m_stringFileInfo;
}

}  // namespace Orc
