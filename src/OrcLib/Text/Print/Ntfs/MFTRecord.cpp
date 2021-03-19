#include "stdafx.h"

#include "Text/Print/Ntfs/MFTRecord.h"

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

}  // namespace detail
}  // namespace Text
}  // namespace Orc
