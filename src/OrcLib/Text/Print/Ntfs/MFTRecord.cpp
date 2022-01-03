//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Text/Print/Ntfs/MFTRecord.h"

#include <string_view>
#include <vector>
#include <string>
#include <algorithm>

#include <fmt/format.h>

#include <MFTRecord.h>

#include "Utils/TypeTraits.h"
#include "Utils/Result.h"
#include "Text/HexDump.h"
#include "Text/Print/Ntfs/AttributeListEntry.h"
#include "Text/Print/Ntfs/NonResidentAttributeExtent.h"
#include "Text/Print/FILE_NAME.h"
#include "Text/Fmt/ByteQuantity.h"

namespace Orc {
namespace Text {

template <typename T>
std::string ToHexString(const boost::dynamic_bitset<T>& bitset)
{
    std::vector<T> bytes;
    bytes.reserve(bitset.num_blocks());
    boost::to_block_range(bitset, std::back_inserter(bytes));

    std::string_view out(
        reinterpret_cast<const char*>(bytes.data()), bytes.size() * sizeof(typename decltype(bytes)::value_type));

    std::string hex;
    ToHex(std::crbegin(bytes), std::crend(bytes), std::back_inserter(hex));
    return hex;
}

const ATTRIBUTE_RECORD_HEADER*
GetAttributeRecordHeader(const std::vector<Orc::AttributeListEntry>& attributes, int typeCode)
{
    const auto attributeIt =
        std::find_if(std::cbegin(attributes), std::cend(attributes), [typeCode](const auto& entry) {
            return entry.TypeCode() == typeCode;
        });

    if (attributeIt == std::cend(attributes))
    {
        return nullptr;
    }

    const auto attribute = attributeIt->Attribute();
    if (attribute == nullptr)
    {
        return nullptr;
    }

    return attribute->Header();
}

std::optional<Traits::ByteQuantity<uint64_t>> GetDataSize(const DataAttribute& data)
{
    const auto attributeHeader = data.Header();

    if (attributeHeader->FormCode == RESIDENT_FORM)
    {
        return Traits::ByteQuantity<uint64_t>(attributeHeader->Form.Resident.ValueLength);
    }
    else if (attributeHeader->Form.Nonresident.LowestVcn == 0)
    {
        // Each cluster of a non-resident stream is given a sequential number. This is its Virtual Cluster Number. VCN 0
        // (zero) refers to the first cluster of the stream.
        return Traits::ByteQuantity<uint64_t>(attributeHeader->Form.Nonresident.FileSize);
    }

    return {};
}

Orc::Result<std::string_view> AttributeTypeToString(ATTRIBUTE_TYPE_CODE code)
{
    switch (code)
    {
        case $UNUSED:
            return std::string("$UNUSED");
        case $STANDARD_INFORMATION:
            return "$STANDARD_INFORMATION";
        case $ATTRIBUTE_LIST:
            return "$ATTRIBUTE_LIST";
        case $FILE_NAME:
            return "$FILE_NAME";
        case $OBJECT_ID:
            return "$OBJECT_ID";
        case $SECURITY_DESCRIPTOR:
            return "$SECURITY_DESCRIPTOR";
        case $VOLUME_NAME:
            return "$VOLUME_NAME";
        case $VOLUME_INFORMATION:
            return "$VOLUME_INFORMATION";
        case $DATA:
            return "$DATA";
        case $INDEX_ROOT:
            return "$INDEX_ROOT";
        case $INDEX_ALLOCATION:
            return "$INDEX_ALLOCATION";
        case $BITMAP:
            return "$BITMAP";
        case $REPARSE_POINT:
            return "$REPARSE_POINT";
        case $EA_INFORMATION:
            return "$EA_INFORMATION";
        case $EA:
            return "$EA";
        case $LOGGED_UTILITY_STREAM:
            return "$LOGGED_UTILITY_STREAM";
        case $FIRST_USER_DEFINED_ATTRIBUTE:
            return "$FIRST_USER_DEFINED_ATTRIBUTE";
        case $END:
            return "$END";
        default:
            return std::make_error_code(std::errc::invalid_argument);
    }
}

void PrintValueFileAttributes(Orc::Text::Tree& root, const std::string& name, ULONG fileAttributes)
{
    const auto attributes = fmt::format(
        "{}{}{}{}{}{}{}{}{}{}{}{}{}",
        fileAttributes & FILE_ATTRIBUTE_ARCHIVE ? 'A' : '.',
        fileAttributes & FILE_ATTRIBUTE_COMPRESSED ? 'C' : '.',
        fileAttributes & FILE_ATTRIBUTE_DIRECTORY ? 'D' : '.',
        fileAttributes & FILE_ATTRIBUTE_ENCRYPTED ? 'E' : '.',
        fileAttributes & FILE_ATTRIBUTE_HIDDEN ? 'H' : '.',
        fileAttributes & FILE_ATTRIBUTE_NORMAL ? 'N' : '.',
        fileAttributes & FILE_ATTRIBUTE_OFFLINE ? 'O' : '.',
        fileAttributes & FILE_ATTRIBUTE_READONLY ? 'R' : '.',
        fileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? 'L' : '.',
        fileAttributes & FILE_ATTRIBUTE_SPARSE_FILE ? 'P' : '.',
        fileAttributes & FILE_ATTRIBUTE_SYSTEM ? 'S' : '.',
        fileAttributes & FILE_ATTRIBUTE_TEMPORARY ? 'T' : '.',
        fileAttributes & FILE_ATTRIBUTE_VIRTUAL ? 'V' : '.');

    PrintValue(root, name, attributes);
}

void PrintFilenameAttribute(Orc::Text::Tree& root, const Orc::MFTRecord& record)
{
    const auto& names = record.GetFileNames();
    if (names.empty())
    {
        return;
    }

    auto node = root.AddNode("$FILE_NAMES");

    const auto& attributes = record.GetAttributeList();
    const auto fileNameAttributeHeader = GetAttributeRecordHeader(attributes, $FILE_NAME);
    for (const auto pName : names)
    {
        if (pName == nullptr)
        {
            Log::Error("Invalid MFT record: {}", record.GetSafeMFTSegmentNumber());
            continue;
        }

        Print(node, *pName);
        if (fileNameAttributeHeader)
        {
            const auto rawName =
                (PFILE_NAME)((LPBYTE)fileNameAttributeHeader + fileNameAttributeHeader->Form.Resident.ValueOffset);
            if (pName == rawName)
            {
                PrintValue(node, "FileNameID", fileNameAttributeHeader->Instance);
            }
        }

        node.AddEOL();
    }
}

// TODO: FIXME
// void PrintCarvedFileEntries(Orc::Text::Tree& root, std::string_view buffer)
//{
//    uint64_t carvedEntryCount = 0;
//
//    for (auto offset = buffer.data(); offset + sizeof(FILE_NAME) < offset + buffer.size(); ++offset)
//    {
//        const auto file_name = reinterpret_cast<const FILE_NAME*>(offset);
//
//        if (NtfsFullSegmentNumber(&file_name->ParentDirectory) != record.GetSafeMFTSegmentNumber())
//        {
//            continue;
//        }
//
//        auto node = root.AddNode("Carved index entry #{} (block #{})", carvedEntryCount++, blockId);
//
//        const PINDEX_ENTRY entry = reinterpret_cast<PINDEX_ENTRY>(offset - sizeof(INDEX_ENTRY));
//        Print(node, *file_name);
//        PrintValue(node, "IndexedAttrOwner", fmt::format("{:#x}", NtfsFullSegmentNumber(&entry->FileReference)));
//
//        root.AddEmptyLine();
//    }
//}

void Print(Orc::Text::Tree& root, const std::string& name, const IndexRootAttribute& attributes)
{
    auto node = root.AddNode(name);
    PrintValue(node, "Name", std::wstring_view(attributes.NamePtr(), attributes.NameLength()));

    std::string type;
    const auto prettyTypeCode = AttributeTypeToString(attributes.IndexedAttributeType());
    if (prettyTypeCode)
    {
        type = fmt::format("{:#x} ({})", attributes.IndexedAttributeType(), prettyTypeCode.value());
    }
    else
    {
        type = fmt::format("{:#x}", attributes.IndexedAttributeType());
    }

    PrintValue(node, "Indexed attribute type", type);
    PrintValue(node, "Size per index", attributes.SizePerIndex());
    PrintValue(node, "Block per index", attributes.BlocksPerIndex());

    node.AddEmptyLine();

    for (auto entry = attributes.FirstIndexEntry(); entry != nullptr; entry = NtfsNextIndexEntry(entry))
    {
        if (entry->Flags & INDEX_ENTRY_END)
        {
            break;
        }

        PFILE_NAME file_name = (PFILE_NAME)((PBYTE)entry + sizeof(INDEX_ENTRY));
        Print(node, *file_name);

        node.AddEmptyLine();
    }
}

void Print(
    Orc::Text::Tree& root,
    const std::string& name,
    IndexAllocationAttribute& attributes,  // cannot be const due to 'GetNonResidentInformation'
    const Orc::MFTRecord& record,
    const Orc::IndexRootAttribute& indexRootAttributes,
    const Orc::BitmapAttribute& bitmap,
    const std::shared_ptr<VolumeReader>& volume)
{
    auto node = root.AddNode(name);
    PrintValue(node, "Name", std::wstring_view(attributes.NamePtr(), attributes.NameLength()));

    if (attributes.IsNonResident())
    {
        const auto* info = attributes.GetNonResidentInformation(volume);
        if (info)
        {
            PrintValue(
                node,
                "Allocated size",
                fmt::format("{} ({})", Traits::ByteQuantity(info->ExtentsSize), info->ExtentsSize));

            for (size_t i = 0; i < info->ExtentsVector.size(); ++i)
            {
                const auto& extent = info->ExtentsVector[i];

                PrintValue(
                    node,
                    fmt::format("Extent #{}", i),
                    fmt::format(
                        "Lowest VCN: {}, Offset: {}, Size: {} ({}), Sparse: {}",
                        extent.LowestVCN,
                        Traits::Offset(extent.DiskOffset),
                        Traits::ByteQuantity(extent.DiskAlloc),
                        extent.DiskAlloc,
                        extent.bZero ? "yes" : "no"));
            }
        }
    }

    uint64_t blockId = 0;
    uint64_t fileCount = 0;
    uint64_t carvedFileCount = 0;

    std::vector<char> buffer;
    buffer.resize(static_cast<size_t>(indexRootAttributes.SizePerIndex()));

    auto stream = attributes.GetDataStream(volume);

    for (uint64_t read = 1; read > 0; ++blockId)
    {
        HRESULT hr = stream->Read(buffer.data(), buffer.capacity(), &read);
        if (FAILED(hr))
        {
            Log::Error("Failed to read $INDEX_ALLOCATION [{}]", SystemError(hr));
            break;
        }

        buffer.resize(read);

        // NOTE: condition was originally 'read <= 0' but reading the following code this seems more appropriate
        if (buffer.size() <= sizeof(INDEX_ALLOCATION_BUFFER))
        {
            Log::Debug(L"Incomplete read of INDEX_ALLOCATION_BUFFER size (read: {})", buffer.size());
            break;
        }

        const PINDEX_ALLOCATION_BUFFER indexAllocationBuffer =
            reinterpret_cast<INDEX_ALLOCATION_BUFFER*>(buffer.data());

        if (bitmap[blockId])
        {
            hr = MFTUtils::MultiSectorFixup(indexAllocationBuffer, indexRootAttributes.SizePerIndex(), volume);
            if (FAILED(hr))
            {
                Log::Debug(L"Failed to fix $INDEX_ALLOCATION header [{}]", SystemError(hr));
                continue;
            }

            uint64_t indexedEntryCount = 0;
            const PINDEX_HEADER header = &(indexAllocationBuffer->IndexHeader);
            for (auto entry = NtfsFirstIndexEntry(header); entry != nullptr; entry = NtfsNextIndexEntry(entry))
            {
                if (entry->Flags & INDEX_ENTRY_END)
                {
                    break;
                }

                auto fileNode =
                    node.AddNode("Block #{}, entry #{}, file #{}", blockId, indexedEntryCount++, fileCount++);

                FILE_NAME* file_name = (PFILE_NAME)((PBYTE)entry + sizeof(INDEX_ENTRY));
                Print(fileNode, *file_name);
                PrintValue(
                    fileNode,
                    "IndexedAttrOwner",
                    fmt::format("{:#016x}", NtfsFullSegmentNumber(&entry->FileReference)));

                fileNode.AddEmptyLine();
            }

            uint64_t carvedEntryCount = 0;

            const char* pFreeBytes = reinterpret_cast<char*>(NtfsFirstIndexEntry(header)) + header->FirstFreeByte;
            const size_t offsetFreeBytes = pFreeBytes - buffer.data();
            const size_t freeBytesCount = buffer.size() - offsetFreeBytes;

            std::string_view freeBytes(pFreeBytes, freeBytesCount);

            for (auto offset = 0; offset + sizeof(FILE_NAME) < freeBytes.size(); ++offset)
            {
                auto* file_name = reinterpret_cast<const FILE_NAME*>(&freeBytes[offset]);

                if (NtfsFullSegmentNumber(&file_name->ParentDirectory) != record.GetSafeMFTSegmentNumber())
                {
                    continue;
                }

                auto carvedNode =
                    node.AddNode("Block #{}, entry #{} (carved), file #{}", blockId, carvedEntryCount++, fileCount++);
                carvedFileCount++;

                auto entry = reinterpret_cast<const INDEX_ENTRY*>(&freeBytes[offset] - sizeof(INDEX_ENTRY));
                Print(carvedNode, *file_name);
                PrintValue(
                    carvedNode,
                    "IndexedAttrOwner",
                    fmt::format("{:#016x}", NtfsFullSegmentNumber(&entry->FileReference)));

                carvedNode.AddEmptyLine();
            }
        }
        else
        {
            hr = MFTUtils::MultiSectorFixup(indexAllocationBuffer, indexRootAttributes.SizePerIndex(), volume);
            if (FAILED(hr))
            {
                Log::Debug(L"Failed to fix $INDEX_ALLOCATION carved header [{}]", SystemError(hr));
                continue;
            }

            uint64_t carvedEntryCount = 0;

            for (size_t offset = sizeof(INDEX_ENTRY); offset + sizeof(FILE_NAME) < buffer.size(); ++offset)
            {
                PFILE_NAME file_name = reinterpret_cast<PFILE_NAME>(buffer.data() + offset);
                if (NtfsFullSegmentNumber(&file_name->ParentDirectory) != record.GetSafeMFTSegmentNumber())
                {
                    continue;
                }

                auto carvedNode =
                    node.AddNode("Block #{} (carved), entry #{}, file #{}", blockId, carvedEntryCount++, fileCount++);
                carvedFileCount++;

                const PINDEX_ENTRY entry = reinterpret_cast<PINDEX_ENTRY>(buffer.data() + offset - sizeof(INDEX_ENTRY));
                Print(carvedNode, *file_name);
                PrintValue(
                    carvedNode,
                    "IndexedAttrOwner",
                    fmt::format("{:#016x}", NtfsFullSegmentNumber(&entry->FileReference)));

                carvedNode.AddEmptyLine();
            }
        }
    }

    PrintValue(
        node,
        "Statistics",
        fmt::format(
            "{} file entries with {} carved in {} blocks of {}",
            fileCount,
            carvedFileCount,
            blockId,
            Traits::ByteQuantity(indexRootAttributes.SizePerIndex())));

    root.AddEmptyLine();
}

void Print(Orc::Text::Tree& root, const std::string& name, const BitmapAttribute& attributes)
{
    auto node = root.AddNode(name);
    PrintValue(node, "Name", std::wstring_view(attributes.NamePtr(), attributes.NameLength()));
    PrintValue(node, "Bitmap", fmt::format("0x{}", ToHexString(attributes.Bits())));
    root.AddEmptyLine();
}

void PrintStandardInformation(Orc::Text::Tree& root, const Orc::MFTRecord& record)
{
    auto standardInformation = record.GetStandardInformation();
    if (standardInformation == nullptr)
    {
        return;
    }

    auto node = root.AddNode("$STANDARD_INFORMATION");
    PrintValueFileAttributes(node, "FileAttributes", record.GetStandardInformation()->FileAttributes);

    PrintValue(node, "CreationTime", record.GetStandardInformation()->CreationTime);
    PrintValue(node, "LastModificationTime", record.GetStandardInformation()->LastModificationTime);
    PrintValue(node, "LastAccessTime", record.GetStandardInformation()->LastAccessTime);
    PrintValue(node, "LastChangeTime", record.GetStandardInformation()->LastChangeTime);
    PrintValue(node, "OwnerID", record.GetStandardInformation()->OwnerId);
    PrintValue(node, "SecurityID", record.GetStandardInformation()->SecurityId);

    root.AddEmptyLine();
}

void PrintIndexAttributes(
    Orc::Text::Tree& root,
    const Orc::MFTRecord& record,
    const std::shared_ptr<VolumeReader>& volume)
{
    if (!record.IsDirectory())
    {
        return;
    }

    std::shared_ptr<IndexRootAttribute> indexRoot;
    std::shared_ptr<IndexAllocationAttribute> indexAllocation;
    std::shared_ptr<BitmapAttribute> bitmap;

    HRESULT hr = record.GetIndexAttributes(volume, L"$I30", indexRoot, indexAllocation, bitmap);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to find $I30 attributes [{}]", SystemError(hr));
        return;
    }

    if (indexRoot)
    {
        Print(root, "$INDEX_ROOT", *indexRoot);
    }

    if (indexAllocation)
    {
        Print(root, "$INDEX_ALLOCATION", *indexAllocation, record, *indexRoot, *bitmap, volume);
    }

    if (bitmap)
    {
        Print(root, "$BITMAP", *bitmap);
    }
}

void PrintExtendedAttributes(
    Orc::Text::Tree& root,
    const Orc::MFTRecord& record,
    const std::shared_ptr<VolumeReader>& volume)
{
    if (!record.HasExtendedAttr())
    {
        return;
    }

    for (const auto& attribute : record.GetAttributeList())
    {
        if (attribute.TypeCode() != $EA)
        {
            continue;
        }

        auto ea = std::dynamic_pointer_cast<ExtendedAttribute, MftRecordAttribute>(attribute.Attribute());
        if (ea == nullptr)
        {
            continue;
        }

        HRESULT hr = ea->Parse(volume);
        if (FAILED(hr))
        {
            Log::Debug("Failed to parse extended attribute [{}]", SystemError(hr));
            continue;
        }

        auto node = root.AddNode("$EA");

        for (size_t i = 0; i < ea->Items().size(); ++i)
        {
            const auto& [name, data] = ea->Items()[i];

            PrintValue(node, "Name", name);
            PrintValue(node, "Size", data.GetCount());
            node.AddHexDump("Data:", std::string_view(reinterpret_cast<const char*>(data.GetData()), data.GetCount()));

            node.AddEmptyLine();
        }
    }
}

void PrintReparsePointAttribute(Orc::Text::Tree& root, const Orc::MFTRecord& record)
{
    if (!record.HasReparsePoint())
    {
        return;
    }

    for (const auto& attribute : record.GetAttributeList())
    {
        if (attribute.TypeCode() == $REPARSE_POINT)
        {
            auto reparsePoint =
                std::dynamic_pointer_cast<ReparsePointAttribute, MftRecordAttribute>(attribute.Attribute());
            if (reparsePoint == nullptr)
            {
                continue;
            }

            HRESULT hr = reparsePoint->Parse();
            if (FAILED(hr))
            {
                continue;
            }

            auto node = root.AddNode("$REPARSE_POINT");

            PrintValue(node, "Flags", fmt::format("{:#x}", static_cast<ULONG>(reparsePoint->Flags())));
            PrintValue(node, "Name", reparsePoint->PrintName());
            PrintValue(node, "Alternate name", reparsePoint->SubstituteName());

            node.AddEmptyLine();
        }
    }
}

void PrintDataAttribute(Orc::Text::Tree& root, const MFTRecord& record, const std::shared_ptr<VolumeReader>& volume)
{
    const auto dataList = record.GetDataAttributes();
    if (dataList.empty())
    {
        return;
    }

    auto node = root.AddNode("$DATA");

    for (size_t i = 0; i < dataList.size(); ++i)
    {
        const auto& data = dataList[i];
        if (data == nullptr)
        {
            Log::Debug("Invalid $DATA entry");
            continue;
        }

        auto entryNode = node.AddNode("Entry #{}", i);
        PrintValue(entryNode, "Name", std::wstring_view(data->NamePtr(), data->NameLength()));

        const auto dataSize = GetDataSize(*data);
        if (dataSize.has_value())
        {
            const auto size = fmt::format("{} ({} bytes)", *dataSize, (*dataSize).value);
            PrintValue(entryNode, "Size", size);
        }

        entryNode.Add("Resident: {}", data->IsResident());
        if (!data->IsResident())
        {
            auto info = data->GetNonResidentInformation(volume);
            if (info == nullptr)
            {
                continue;
            }

            auto extentsNode = entryNode.AddNode("Extents ({})", Traits::ByteQuantity(info->ExtentsSize));
            if (info->ExtentsVector.empty())
            {
                continue;
            }

            for (size_t j = 0; j < info->ExtentsVector.size(); ++j)
            {
                const auto& extent = info->ExtentsVector[j];
                Print(extentsNode, extent);
            }
        }
        else
        {
            std::string_view content(
                reinterpret_cast<const char*>(data->Header()) + data->Header()->Form.Resident.ValueOffset,
                data->Header()->Form.Resident.ValueLength);

            entryNode.AddHexDump("Data:", content);
        }
    }
}

void Print(Orc::Text::Tree& root, const MFTRecord& record, const std::shared_ptr<VolumeReader>& volume)
{
    if (!volume)
    {
        return;
    }

    auto recordNode = root.AddNode(
        "MFT record {:#018x} {}{}{}{}{}{}{}{}{}",
        record.GetSafeMFTSegmentNumber(),
        record.IsRecordInUse() ? "[in_use]" : "[deleted]",
        record.IsDirectory() ? "[directory]" : "",
        record.IsBaseRecord() ? "[base]" : "[child]",
        record.IsJunction() ? "[junction]" : "",
        record.IsOverlayFile() ? "[overlay]" : "",
        record.IsSymbolicLink() ? "[symlink]" : "",
        record.HasExtendedAttr() ? "[extended attr]" : "",
        record.HasNamedDataAttr() ? "[named $DATA]" : "",
        record.HasReparsePoint() ? "[reparse point]" : "");

    const auto& childrens = record.GetChildRecords();
    if (!childrens.empty())
    {
        auto childrensNode = recordNode.AddNode("Children records");

        for (const auto& [childFRN, childRecord] : childrens)
        {
            PrintValue(childrensNode, "FRN", Traits::Offset(childFRN));
        }

        childrensNode.AddEmptyLine();
    }

    // PrintValues
    const auto& attributes = record.GetAttributeList();
    if (!attributes.empty())
    {
        auto attributesNode = recordNode.AddNode("Attributes");

        for (const auto& attribute : attributes)
        {
            Print(attributesNode, attribute);
        }

        attributesNode.AddEmptyLine();
    }

    PrintStandardInformation(recordNode, record);
    PrintFilenameAttribute(recordNode, record);
    PrintIndexAttributes(recordNode, record, volume);
    PrintExtendedAttributes(recordNode, record, volume);
    PrintReparsePointAttribute(recordNode, record);
    PrintDataAttribute(recordNode, record, volume);
}

}  // namespace Text
}  // namespace Orc
