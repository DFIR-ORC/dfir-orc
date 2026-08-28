//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Utilities/VersionInfo.h"

#include <algorithm>

// The RT_VERSION resource is a tree of VS_VERSIONINFO nodes. Each node is:
//
//     WORD  wLength;        // total node length in bytes, EXCLUDING trailing alignment padding
//     WORD  wValueLength;   // Value length (WCHARs for text nodes, bytes for binary nodes)
//     WORD  wType;          // 1 = text, 0 = binary
//     WCHAR szKey[];        // null-terminated
//     WORD  Padding[];      // aligns Value to a 32-bit boundary (relative to the resource start)
//     BYTE  Value[];        // wValueLength units
//     WORD  Padding2[];     // aligns the first child to a 32-bit boundary
//     Node  Children[];     // each aligned to a 32-bit boundary
//
// There is no Win32 API to edit a string value, so the tree is parsed into VerNode, the target string is
// modified, and the tree is serialized back byte-for-byte. All offsets are validated; malformed input is
// rejected without touching the caller's buffer.

namespace {

constexpr size_t kNodeHeaderSize = 6;  // wLength + wValueLength + wType

struct VerNode
{
    std::wstring key;
    uint16_t type = 0;
    std::vector<uint8_t> value;  // raw value bytes (for text nodes: WCHARs including the null terminator)
    std::vector<VerNode> children;
};

size_t Align4(size_t offset)
{
    return (offset + 3) & ~static_cast<size_t>(3);
}

uint16_t ReadU16(const std::vector<uint8_t>& buffer, size_t offset)
{
    return static_cast<uint16_t>(buffer[offset]) | static_cast<uint16_t>(static_cast<uint16_t>(buffer[offset + 1]) << 8);
}

void WriteU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void PadTo4(std::vector<uint8_t>& out)
{
    while ((out.size() & 3u) != 0)
    {
        out.push_back(0);
    }
}

std::error_code BadMessage()
{
    return std::make_error_code(std::errc::bad_message);
}

// Parse one node starting at 'offset', bounded by 'limit'. On success, 'nodeLength' receives the node's
// wLength (unpadded) so the caller can advance to the next sibling.
std::error_code ParseNode(
    const std::vector<uint8_t>& buffer,
    size_t offset,
    size_t limit,
    VerNode& node,
    size_t& nodeLength)
{
    if (offset + kNodeHeaderSize > limit)
    {
        return BadMessage();
    }

    const uint16_t wLength = ReadU16(buffer, offset);
    const uint16_t wValueLength = ReadU16(buffer, offset + 2);
    const uint16_t wType = ReadU16(buffer, offset + 4);

    const size_t nodeEnd = offset + wLength;
    if (wLength < kNodeHeaderSize || nodeEnd > limit)
    {
        return BadMessage();
    }

    // Key: null-terminated wide string within the node.
    std::wstring key;
    size_t cursor = offset + kNodeHeaderSize;
    for (;;)
    {
        if (cursor + 2 > nodeEnd)
        {
            return BadMessage();
        }

        const uint16_t ch = ReadU16(buffer, cursor);
        cursor += 2;
        if (ch == 0)
        {
            break;
        }

        key.push_back(static_cast<wchar_t>(ch));
    }

    // wLength excludes trailing padding, so a value-less node whose key ends unaligned legitimately has
    // Align4(cursor) past nodeEnd: clamp the start so no iterator is ever formed beyond the node.
    const size_t valueStart = std::min(Align4(cursor), nodeEnd);
    size_t valueBytes = (wType == 1) ? static_cast<size_t>(wValueLength) * 2 : static_cast<size_t>(wValueLength);
    if (valueBytes > nodeEnd - valueStart)
    {
        // Tolerate an over-stated length by clamping to the node bounds rather than rejecting.
        valueBytes = nodeEnd - valueStart;
    }

    node.key = std::move(key);
    node.type = wType;
    node.value.assign(buffer.begin() + valueStart, buffer.begin() + valueStart + valueBytes);

    size_t childStart = Align4(valueStart + valueBytes);
    while (childStart < nodeEnd)
    {
        VerNode child;
        size_t childLength = 0;
        auto ec = ParseNode(buffer, childStart, nodeEnd, child, childLength);
        if (ec)
        {
            return ec;
        }

        if (childLength == 0)
        {
            return BadMessage();  // no forward progress: malformed
        }

        node.children.push_back(std::move(child));
        childStart = Align4(childStart + childLength);
    }

    nodeLength = wLength;
    return {};
}

// Serialize a node (and its subtree) into 'out', which must already hold the bytes that precede it so that
// 32-bit alignment padding is computed against the true resource offset. Fails when a node no longer fits
// its 16-bit length fields (the marker can push a resource that was already near 64 KB over the edge).
std::error_code SerializeNode(const VerNode& node, std::vector<uint8_t>& out)
{
    const size_t start = out.size();

    const size_t valueUnits = (node.type == 1) ? node.value.size() / 2 : node.value.size();
    if (valueUnits > UINT16_MAX)
    {
        return std::make_error_code(std::errc::value_too_large);
    }

    WriteU16(out, 0);  // wLength placeholder, patched below
    WriteU16(out, static_cast<uint16_t>(valueUnits));
    WriteU16(out, node.type);

    for (wchar_t ch : node.key)
    {
        WriteU16(out, static_cast<uint16_t>(ch));
    }
    WriteU16(out, 0);  // null terminator

    PadTo4(out);
    out.insert(out.end(), node.value.begin(), node.value.end());

    for (const auto& child : node.children)
    {
        PadTo4(out);  // align child start; internal padding is counted in wLength
        auto ec = SerializeNode(child, out);
        if (ec)
        {
            return ec;
        }
    }

    // Patch wLength: the node spans from 'start' to the current end, excluding any trailing padding.
    const size_t length = out.size() - start;
    if (length > UINT16_MAX)
    {
        return std::make_error_code(std::errc::value_too_large);
    }

    const auto wLength = static_cast<uint16_t>(length);
    out[start] = static_cast<uint8_t>(wLength & 0xFF);
    out[start + 1] = static_cast<uint8_t>((wLength >> 8) & 0xFF);
    return {};
}

VerNode* FindChild(VerNode& parent, const std::wstring& key)
{
    for (auto& child : parent.children)
    {
        if (child.key == key)
        {
            return &child;
        }
    }

    return nullptr;
}

std::wstring DecodeStringValue(const std::vector<uint8_t>& value)
{
    std::wstring text(value.size() / 2, L'\0');
    for (size_t i = 0; i < text.size(); ++i)
    {
        text[i] = static_cast<wchar_t>(static_cast<uint16_t>(value[i * 2]) | (static_cast<uint16_t>(value[i * 2 + 1]) << 8));
    }

    // Drop a single trailing null terminator if present.
    if (!text.empty() && text.back() == L'\0')
    {
        text.pop_back();
    }

    return text;
}

std::vector<uint8_t> EncodeStringValue(const std::wstring& text)
{
    std::vector<uint8_t> value;
    value.reserve((text.size() + 1) * 2);
    for (wchar_t ch : text)
    {
        value.push_back(static_cast<uint8_t>(static_cast<uint16_t>(ch) & 0xFF));
        value.push_back(static_cast<uint8_t>((static_cast<uint16_t>(ch) >> 8) & 0xFF));
    }
    value.push_back(0);  // null terminator (WCHAR)
    value.push_back(0);

    return value;
}

}  // namespace

std::error_code AppendFileDescriptionMarker(std::vector<uint8_t>& versionResource, const std::wstring& marker)
{
    if (marker.empty())
    {
        return {};
    }

    VerNode root;
    size_t rootLength = 0;
    auto ec = ParseNode(versionResource, 0, versionResource.size(), root, rootLength);
    if (ec)
    {
        return ec;
    }

    VerNode* stringFileInfo = FindChild(root, L"StringFileInfo");
    if (!stringFileInfo || stringFileInfo->children.empty())
    {
        // No string table to carry the marker.
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    // Use the first string table (the preferred language/codepage block).
    VerNode& stringTable = stringFileInfo->children.front();

    VerNode* fileDescription = FindChild(stringTable, L"FileDescription");
    if (fileDescription)
    {
        std::wstring text = DecodeStringValue(fileDescription->value);

        // Idempotent: do not append the marker twice.
        if (text.size() < marker.size() || text.compare(text.size() - marker.size(), marker.size(), marker) != 0)
        {
            text += marker;
        }

        fileDescription->value = EncodeStringValue(text);
        fileDescription->type = 1;
    }
    else
    {
        // No FileDescription: insert one carrying the marker (drop a single leading space if present).
        std::wstring text = (!marker.empty() && marker.front() == L' ') ? marker.substr(1) : marker;

        VerNode node;
        node.key = L"FileDescription";
        node.type = 1;
        node.value = EncodeStringValue(text);
        stringTable.children.push_back(std::move(node));
    }

    std::vector<uint8_t> rebuilt;
    rebuilt.reserve(versionResource.size() + marker.size() * 2 + 32);
    ec = SerializeNode(root, rebuilt);
    if (ec)
    {
        return ec;
    }

    versionResource = std::move(rebuilt);
    return {};
}
