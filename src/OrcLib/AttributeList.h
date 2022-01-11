//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include <vector>

#include "NtfsDataStructures.h"
#include "MFTUtils.h"
#include "VolumeReader.h"
#include "MftRecordAttribute.h"

#pragma managed(push, off)

namespace Orc {

class MFTRecord;
class MFTWalker;
class AttributeList;

class AttributeListEntry
{
    friend class MFTRecord;
    friend class MFTWalker;
    friend class AttributeList;

    PATTRIBUTE_LIST_ENTRY m_pListEntry;

    std::shared_ptr<MftRecordAttribute> m_Attribute;

public:
    AttributeListEntry()
        : m_pListEntry(nullptr)
        , m_Attribute(nullptr) {};
    AttributeListEntry(const AttributeListEntry&) = default;
    AttributeListEntry(AttributeListEntry&&) = default;

    AttributeListEntry(PATTRIBUTE_LIST_ENTRY pListEntry)
        : m_pListEntry(pListEntry)
        , m_Attribute(nullptr)
    {
    }
    AttributeListEntry(const std::shared_ptr<MftRecordAttribute>& pAttr)
        : m_pListEntry(nullptr)
        , m_Attribute(pAttr) {};

    AttributeListEntry& operator=(const AttributeListEntry&) = default;
    AttributeListEntry& operator=(AttributeListEntry&&) noexcept = default;

    ATTRIBUTE_TYPE_CODE TypeCode() const
    {
        if (m_Attribute != nullptr)
            return m_Attribute->Header()->TypeCode;
        if (m_pListEntry != nullptr)
            return m_pListEntry->AttributeTypeCode;
        return 0;
    }
    LPCWSTR TypeStr() const { return TypeStr(TypeCode()); }
    static LPCWSTR TypeStr(ATTRIBUTE_TYPE_CODE aCode);

    UCHAR FormCode() const
    {
        if (m_Attribute)
            return m_Attribute->Header()->FormCode;
        return 0;
    }

    USHORT Flags() const
    {
        if (m_Attribute != nullptr)
            return m_Attribute->Header()->Flags;
        return 0;
    }

    USHORT Instance() const
    {
        if (m_Attribute != nullptr)
            return m_Attribute->Header()->Instance;
        return 0;
    }

    VCN LowestVCN() const
    {
        if (m_Attribute != nullptr && m_Attribute->Header()->FormCode == NONRESIDENT_FORM)
            return m_Attribute->Header()->Form.Nonresident.LowestVcn;
        if (m_pListEntry != nullptr)
            return m_pListEntry->LowestVcn;
        return (LONGLONG)0;
    }

    const WCHAR* AttributeName() const
    {
        if (m_Attribute)
            return m_Attribute->NamePtr();
        if (m_pListEntry)
            return m_pListEntry->AttributeName;
        return NULL;
    }

    DWORD AttributeNameLength() const
    {
        if (m_Attribute)
            return m_Attribute->NameLength();
        if (m_pListEntry)
            return m_pListEntry->AttributeNameLength;
        return 0;
    }

    bool DataIsPlausible() const;

    MFTUtils::SafeMFTSegmentNumber HostRecordSegmentNumber() const;

    const std::shared_ptr<MftRecordAttribute>& Attribute() const { return m_Attribute; }

    bool operator==(const AttributeListEntry& theOther) const;

    ~AttributeListEntry() { m_pListEntry = nullptr; }
};

class AttributeList : public MftRecordAttribute
{
    friend class MFTRecord;
    friend class MFTWalker;

private:
    std::vector<AttributeListEntry> m_AttList;
    CBinaryBuffer m_RawAttrList;

    AttributeListEntry* FindEntryToContinue(const std::shared_ptr<MftRecordAttribute>& pAttribute);

public:
    AttributeList() {}

    AttributeList(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord)
    {
    }

    HRESULT ParseAttributeList(
        const std::shared_ptr<VolumeReader>& pVol,
        const FILE_REFERENCE& RecordReferenceNumber,
        MFTRecord* pRecord);

    HRESULT AddContinuationAttribute(const std::shared_ptr<MftRecordAttribute>& pAttribute);

    bool IsPresent() const
    {
        if (m_pHeader == NULL)
            return false;
        if (m_AttList.size())
            return true;
        return false;
    }

    AttributeList& operator=(const AttributeList& theOther);
    AttributeList& operator=(AttributeList&& theOther);

    HRESULT DeleteAttributesForThisRecord();
};
}  // namespace Orc

#pragma managed(pop)
