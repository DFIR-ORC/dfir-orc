//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

#include <MFTRecord.h>
#include "Utils/TypeTraits.h"
#include "Output/Text/Print/Ntfs/AttributeListEntry.h"
#include "Output/Text/Print/Ntfs/NonResidentAttributeExtent.h"

namespace Orc {
namespace Text {

namespace detail {

const ATTRIBUTE_RECORD_HEADER*
GetAttributeRecordHeader(const std::vector<Orc::AttributeListEntry>& attributes, int typeCode);

std::optional<Traits::ByteQuantity<uint64_t>> GetDataSize(const DataAttribute& data);

}  // namespace detail

template <typename T>
void Print(Orc::Text::Tree<T>& root, const MFTRecord& record, const std::shared_ptr<VolumeReader>& volume)
{
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
            PrintValue(childrensNode, "FRN", childFRN);
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

    auto standardInfoNode = recordNode.AddNode("$STANDARD_INFORMATION");
    PrintValue(standardInfoNode, "FileAttributes", record.GetStandardInformation()->FileAttributes);
    PrintValue(standardInfoNode, "CreationTime", record.GetStandardInformation()->CreationTime);
    PrintValue(standardInfoNode, "LastModificationTime", record.GetStandardInformation()->LastModificationTime);
    PrintValue(standardInfoNode, "LastAccessTime", record.GetStandardInformation()->LastAccessTime);
    PrintValue(standardInfoNode, "LastChangeTime", record.GetStandardInformation()->LastChangeTime);
    PrintValue(standardInfoNode, "OwnerID", record.GetStandardInformation()->OwnerId);
    PrintValue(standardInfoNode, "SecurityID", record.GetStandardInformation()->SecurityId);
    standardInfoNode.AddEmptyLine();

    const auto& names = record.GetFileNames();
    if (!names.empty())
    {
        const auto& attributes = record.GetAttributeList();

        auto fileNamesNode = recordNode.AddNode(L"$FILE_NAMES");

        const auto fileNameAttributeHeader = detail::GetAttributeRecordHeader(attributes, $FILE_NAME);
        for (const auto pName : names)
        {
            if (pName == nullptr)
            {
                spdlog::error("Invalid MFT record: {}", record.GetSafeMFTSegmentNumber());
                continue;
            }

            fileNamesNode.AddWithoutEOL("Name: {}", *pName);
            if (fileNameAttributeHeader)
            {
                const auto rawName =
                    (PFILE_NAME)((LPBYTE)fileNameAttributeHeader + fileNameAttributeHeader->Form.Resident.ValueOffset);
                if (pName == rawName)
                {
                    fileNamesNode.Append(", FileNameID: {}", fileNameAttributeHeader->Instance);
                }
            }

            fileNamesNode.AddEOL();
        }

        recordNode.AddEmptyLine();
    }

    const auto dataList = record.GetDataAttributes();
    if (!dataList.empty())
    {
        auto dataNode = recordNode.AddNode("$DATA");

        for (size_t i = 0; i < dataList.size(); ++i)
        {
            const auto& data = dataList[i];
            if (data == nullptr)
            {
                spdlog::debug("Invalid data entry");
                continue;
            }

            std::wstring nodeName;
            const auto dataName = std::wstring_view(data->NamePtr(), data->NameLength());
            if (dataName.empty())
            {
                nodeName = fmt::format(L"DATA[{}]", i);
            }
            else
            {
                nodeName = fmt::format(L"DATA[{}] Name: '{}'", i, dataName);
            }

            auto entryNode = dataNode.AddNode(nodeName);

            const auto dataSize = detail::GetDataSize(*data);
            if (dataSize)
            {
                entryNode.Add("Size: {} ({:#x})", *dataSize, dataSize->value);
            }
            else
            {
                entryNode.Add("Size: N/A");
            }

            entryNode.Add("Resident: {}", data->IsResident());
            if (!data->IsResident())
            {
                auto info = data->GetNonResidentInformation(volume);
                if (info)
                {
                    auto extentsNode =
                        entryNode.AddNode("Extents  (size: {})", Traits::ByteQuantity(info->ExtentsSize));
                    if (!info->ExtentsVector.empty())
                    {
                        for (size_t j = 0; j < info->ExtentsVector.size(); ++j)
                        {
                            const auto& extent = info->ExtentsVector[i];
                            Print(extentsNode, extent);
                        }
                    }
                }
            }
            else
            {
                std::string_view content(
                    reinterpret_cast<const char*>(data->Header()) + data->Header()->Form.Resident.ValueOffset,
                    data->Header()->Form.Resident.ValueLength);

                entryNode.AddHexDump("Data:"sv, content);
            }
        }
    }

    // if (record.IsDirectory() && volume)
    //{
    //    FormatDirectoryAttributes(root, record, volume, std::error_code());
    //}

    // if (!attributes.empty())
    //{
    //    auto attributesNode = root.AddNode("Attributes");

    //    for (size_t i = 0; i < attributes.size(); ++i)
    //    {
    //        auto attributeNode = attributesNode.AddNode("Attribute #{}", i);
    //        ::Format(attributeNode, attributes[i].Attribute(), volume);
    //    }
    //}
}

}  // namespace Text
}  // namespace Orc
