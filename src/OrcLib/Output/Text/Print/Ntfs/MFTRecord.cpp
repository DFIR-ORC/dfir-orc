#include "stdafx.h"

#include "Output/Text/Print/Ntfs/MFTRecord.h"

namespace Orc {
namespace Text {
namespace detail {

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

}  // namespace detail
}  // namespace Text
}  // namespace Orc
