//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "MFTRecord.h"
#include "MftRecordAttribute.h"
#include "AttributeList.h"

#include "VolumeReader.h"

#include <algorithm>

using namespace std;

using namespace Orc;

bool AttributeListEntry::operator==(const AttributeListEntry& theOther) const
{

    // This is not "strict" equality, this is about finding a "compatible", must be unique entry for this attribute

    if (m_pListEntry != NULL && theOther.m_pListEntry != NULL)
        return false;
    if (theOther.m_Attribute != NULL && m_Attribute != NULL)
        return false;

    if ((m_pListEntry != NULL && theOther.m_Attribute != NULL)
        && (theOther.m_pListEntry == NULL && m_Attribute == NULL))
    {
        // Good, expected possibility :-)
        if (m_pListEntry->AttributeTypeCode != theOther.m_Attribute->m_pHeader->TypeCode)
            return false;
        if (m_pListEntry->AttributeNameLength != theOther.m_Attribute->m_pHeader->NameLength)
            return false;
        if (m_pListEntry->AttributeNameLength == 0 && theOther.m_Attribute->m_pHeader->NameLength == 0)
        {
            if (m_pListEntry->LowestVcn == theOther.m_Attribute->m_LowestVcn)
                return true;
        }
        else if (!wcsncmp(
                     m_pListEntry->AttributeName,
                     (LPWSTR)((BYTE*)theOther.m_Attribute->m_pHeader + theOther.m_Attribute->m_pHeader->NameOffset),
                     m_pListEntry->AttributeNameLength))
        {
            if (m_pListEntry->LowestVcn == theOther.m_Attribute->m_LowestVcn)
                return true;
        }
    }
    else if (
        (theOther.m_pListEntry != NULL && m_Attribute != NULL)
        && (m_pListEntry == NULL && theOther.m_Attribute == NULL))
    {
        // the other Good, expected possibility :-)
        if (theOther.m_pListEntry->AttributeTypeCode != m_Attribute->m_pHeader->TypeCode)
            return false;
        if (theOther.m_pListEntry->AttributeNameLength != m_Attribute->m_pHeader->NameLength)
            return false;
        if (theOther.m_pListEntry->AttributeNameLength == 0 && m_Attribute->m_pHeader->NameLength == 0)
        {
            if (theOther.m_pListEntry->LowestVcn == m_Attribute->m_LowestVcn)
                return true;
        }
        else if (!wcsncmp(
                     theOther.m_pListEntry->AttributeName,
                     (LPWSTR)((BYTE*)m_Attribute->m_pHeader + m_Attribute->m_pHeader->NameOffset),
                     theOther.m_pListEntry->AttributeNameLength))
        {
            if (theOther.m_pListEntry->LowestVcn == m_Attribute->m_LowestVcn)
                return true;
        }
    }
    return false;
}

LPCWSTR AttributeListEntry::TypeStr(ATTRIBUTE_TYPE_CODE aCode)
{
    switch (aCode)
    {
        case $UNUSED:
            return L"$UNUSED";
        case $STANDARD_INFORMATION:
            return L"$STANDARD_INFORMATION";
        case $ATTRIBUTE_LIST:
            return L"$ATTRIBUTE_LIST";
        case $FILE_NAME:
            return L"$FILE_NAME";
        case $OBJECT_ID:
            return L"$OBJECT_ID";
        case $SECURITY_DESCRIPTOR:
            return L"$SECURITY_DESCRIPTOR";
        case $VOLUME_NAME:
            return L"$VOLUME_NAME";
        case $VOLUME_INFORMATION:
            return L"$VOLUME_INFORMATION";
        case $DATA:
            return L"$DATA";
        case $INDEX_ROOT:
            return L"$INDEX_ROOT";
        case $INDEX_ALLOCATION:
            return L"$INDEX_ALLOCATION";
        case $BITMAP:
            return L"$BITMAP";
        case $REPARSE_POINT:
            return L"$REPARSE_POINT";
        case $EA_INFORMATION:
            return L"$EA_INFORMATION";
        case $EA:
            return L"$EA";
        case $LOGGED_UTILITY_STREAM:
            return L"$LOGGED_UTILITY_STREAM";
        case $FIRST_USER_DEFINED_ATTRIBUTE:
            return L"$FIRST_USER_DEFINED_ATTRIBUTE";
        case $END:
            return L"$END";
        default:
            return L"UnknownType";
    }
}

AttributeList& AttributeList::operator=(const AttributeList& theOther)
{

    if (theOther.m_pHeader != NULL && m_pHeader == NULL)
    {
        m_pHeader = theOther.m_pHeader;
        m_RawAttrList.SetCount(theOther.m_RawAttrList.GetCount());
        CopyMemory(m_RawAttrList.GetData(), theOther.m_RawAttrList.GetData(), m_RawAttrList.GetCount());

        // We save the original list of attributes in orig
        std::vector<AttributeListEntry> orig;
        std::swap(orig, m_AttList);

        // we empty the current list & copy theOther content
        m_AttList.assign(theOther.m_AttList.begin(), theOther.m_AttList.end());
        m_pHeader = theOther.m_pHeader;

        // we "update" the list of attributes with interesting information from orig

        for (auto iter = begin(orig); iter != end(orig); ++iter)
        {
            const AttributeListEntry& origattr = *iter;

            auto newpos = std::find(m_AttList.begin(), m_AttList.end(), origattr);

            if (newpos != m_AttList.end())
            {
                AttributeListEntry& newattr = *newpos;

                if (newattr.m_Attribute == NULL && origattr.m_Attribute != NULL)
                {
                    newattr.m_Attribute = origattr.m_Attribute;
                }
            }
        }
    }
    else if (theOther.m_pHeader == NULL && m_pHeader != NULL)
    {
        // the destination has the parsed $ATTRIBUTE_LIST info, we don't copy m_pHeader nor m_RawAttrList.
        // we only copy the interesting info from theOther

        // we update the list with interesting info from theOther

        for (auto iter = theOther.m_AttList.begin(); iter != theOther.m_AttList.end(); ++iter)
        {
            const AttributeListEntry& newattr = *iter;

            auto dstpos = std::find(m_AttList.begin(), m_AttList.end(), newattr);
            _ASSERT(dstpos != m_AttList.end());
            if (dstpos == m_AttList.end())
                continue;

            AttributeListEntry& dstattr = *dstpos;

            if (dstattr.m_Attribute == NULL && newattr.m_Attribute != NULL)
            {
                dstattr.m_Attribute = newattr.m_Attribute;
            }
        }
    }
    return *this;
}

AttributeList& AttributeList::operator=(AttributeList&& theOther)
{
    if (theOther.m_pHeader != NULL && m_pHeader == NULL)
    {
        m_pHeader = theOther.m_pHeader;
        m_RawAttrList.operator=(std::move(theOther.m_RawAttrList));

        // We save the original list of attributes in orig
        std::vector<AttributeListEntry> orig;
        orig.assign(m_AttList.begin(), m_AttList.end());

        // we empty the current list & copy theOther content
        m_AttList = std::move(theOther.m_AttList);
        m_pHeader = theOther.m_pHeader;

        // we "update" the list of attributes with interesting information from orig

        for (auto iter = begin(orig); iter != end(orig); ++iter)
        {
            const AttributeListEntry& origattr = *iter;

            auto newpos = std::find(begin(m_AttList), end(m_AttList), origattr);

            if (newpos != end(m_AttList))
            {
                AttributeListEntry& newattr = *newpos;

                if (newattr.m_Attribute == NULL && origattr.m_Attribute != NULL)
                {
                    newattr.m_Attribute = origattr.m_Attribute;
                }
            }
        }
    }
    else if (theOther.m_pHeader == NULL && m_pHeader != NULL)
    {
        // the destination has the parsed $ATTRIBUTE_LIST info, we don't copy m_pHeader nor m_RawAttrList.
        // we only copy the interesting info from theOther

        // we update the list with interesting info from theOther

        for (auto iter = theOther.m_AttList.begin(); iter != theOther.m_AttList.end(); ++iter)
        {
            const AttributeListEntry& newattr = *iter;

            auto dstpos = std::find(m_AttList.begin(), m_AttList.end(), newattr);

            _ASSERT(dstpos != m_AttList.end());
            if (dstpos == m_AttList.end())
                continue;

            AttributeListEntry& dstattr = *dstpos;

            if (dstattr.m_Attribute == NULL && newattr.m_Attribute != NULL)
            {
                dstattr.m_Attribute = newattr.m_Attribute;
            }
        }
    }
    return *this;
}

bool AttributeListEntry::DataIsPlausible() const
{
    if (m_pListEntry != nullptr)
    {
        switch (m_pListEntry->AttributeTypeCode)
        {
            case $STANDARD_INFORMATION:
            case $ATTRIBUTE_LIST:
            case $FILE_NAME:
            case $OBJECT_ID:
            case $SECURITY_DESCRIPTOR:
            case $VOLUME_NAME:
            case $VOLUME_INFORMATION:
            case $DATA:
            case $INDEX_ROOT:
            case $INDEX_ALLOCATION:
            case $BITMAP:
            case $REPARSE_POINT:
            case $EA_INFORMATION:
            case $EA:
            case $LOGGED_UTILITY_STREAM:
            case $FIRST_USER_DEFINED_ATTRIBUTE:
            case $END:
                break;
            default:
                return false;
        }
        if (m_pListEntry->SegmentReference.SegmentNumberHighPart == 0
            && m_pListEntry->SegmentReference.SegmentNumberLowPart == 0
            && m_pListEntry->SegmentReference.SequenceNumber == 0)
            return false;
        if (m_pListEntry->RecordLength == 0)
            return false;
    }
    if (m_Attribute != nullptr && m_Attribute->Header() != nullptr)
    {
        switch (m_Attribute->Header()->TypeCode)
        {
            case $STANDARD_INFORMATION:
            case $ATTRIBUTE_LIST:
            case $FILE_NAME:
            case $OBJECT_ID:
            case $SECURITY_DESCRIPTOR:
            case $VOLUME_NAME:
            case $VOLUME_INFORMATION:
            case $DATA:
            case $INDEX_ROOT:
            case $INDEX_ALLOCATION:
            case $BITMAP:
            case $REPARSE_POINT:
            case $EA_INFORMATION:
            case $EA:
            case $LOGGED_UTILITY_STREAM:
            case $FIRST_USER_DEFINED_ATTRIBUTE:
            case $END:
                break;
            default:
                return false;
        }
        if (m_Attribute->Header()->RecordLength == 0)
            return false;
    }

    return true;
}

MFTUtils::SafeMFTSegmentNumber AttributeListEntry::HostRecordSegmentNumber() const
{
    if (m_pListEntry != nullptr)
        return NtfsFullSegmentNumber(&m_pListEntry->SegmentReference);
    if (m_Attribute != nullptr)
        return NtfsFullSegmentNumber(&m_Attribute->m_pHostRecord->GetFileReferenceNumber());
    return 0;
}

HRESULT
AttributeList::ParseAttributeList(const std::shared_ptr<VolumeReader>& pVol, const FILE_REFERENCE&, MFTRecord* pRecord)
{
    HRESULT hr = E_FAIL;
    if (m_pHeader == NULL)
        return E_POINTER;

    ULONGLONG ullDataSize = 0;

    if (FAILED(hr = DataSize(pVol, ullDataSize)))
        return hr;

    ULONGLONG ullBytesRead = 0;

    shared_ptr<MftRecordAttribute> attr = shared_from_this();

    if (FAILED(hr = pRecord->ReadData(pVol, attr, 0L, ullDataSize, m_RawAttrList, &ullBytesRead)))
        return hr;

    ATTRIBUTE_LIST_ENTRY* pListEntry = (ATTRIBUTE_LIST_ENTRY*)(m_RawAttrList.GetData());

    DWORD dwCurPos = 0;

    do
    {
        AttributeListEntry ale(pListEntry);

        if (ale.DataIsPlausible())
        {
            if (pListEntry->AttributeTypeCode == $EA)
                pRecord->m_bHasExtendedAttr = true;
            if (pListEntry->AttributeTypeCode == $DATA && pListEntry->AttributeNameLength > 0)
                pRecord->m_bHasNamedDataAttr = true;

            m_AttList.push_back(ale);
        }
        if (pListEntry->RecordLength == 0)
            break;

        dwCurPos += pListEntry->RecordLength;
        pListEntry = (ATTRIBUTE_LIST_ENTRY*)(((BYTE*)pListEntry + pListEntry->RecordLength));

    } while (dwCurPos < m_RawAttrList.GetCount());

    return S_OK;
}

AttributeListEntry* AttributeList::FindEntryToContinue(const shared_ptr<MftRecordAttribute>& pAttribute)
{
    if (pAttribute == NULL)
        return NULL;

    for (auto iter = m_AttList.begin(); iter != m_AttList.end(); ++iter)
    {
        AttributeListEntry& entry = *iter;

        if (entry.m_Attribute != NULL)
        {
            if (entry.m_Attribute->m_LowestVcn != 0)
                continue;  // an attribute to continue must be have a LowestVCN of 0

            if (entry.m_Attribute->m_pHeader->TypeCode != pAttribute->m_pHeader->TypeCode)
                continue;
            if (entry.m_Attribute->m_pHeader->NameLength != pAttribute->m_pHeader->NameLength)
                continue;

            if (pAttribute->m_pHeader->NameLength == 0)
            {
                return &entry;
            }

            if (!wcsncmp(
                    (LPWSTR)((BYTE*)entry.m_Attribute->m_pHeader + entry.m_Attribute->m_pHeader->NameOffset),
                    (LPWSTR)((BYTE*)pAttribute->m_pHeader + pAttribute->m_pHeader->NameOffset),
                    pAttribute->m_pHeader->NameLength))
            {
                return &entry;
            }
        }
        else if (entry.m_pListEntry != NULL)
        {
            if (entry.m_pListEntry->LowestVcn != 0)
                continue;

            if (entry.m_pListEntry->AttributeTypeCode != pAttribute->m_pHeader->TypeCode)
                continue;
            if (entry.m_pListEntry->AttributeNameLength != pAttribute->m_pHeader->NameLength)
                continue;

            if (pAttribute->m_pHeader->NameLength == 0)
            {
                return &entry;
            }

            if (!wcsncmp(
                    entry.m_pListEntry->AttributeName,
                    (LPWSTR)((BYTE*)pAttribute->m_pHeader + pAttribute->m_pHeader->NameOffset),
                    entry.m_pListEntry->AttributeNameLength))
            {
                return &entry;
            }
        }
    }

    return NULL;
}

HRESULT AttributeList::AddContinuationAttribute(const shared_ptr<MftRecordAttribute>& pAttribute)
{
    _ASSERT(pAttribute->m_LowestVcn != 0);

    AttributeListEntry* pEntry = FindEntryToContinue(pAttribute);

    if (pEntry != nullptr)
    {
        if (pEntry->m_Attribute != nullptr)
            pEntry->m_Attribute->AddContinuationAttribute(pAttribute);
        else if (pEntry->m_pListEntry != nullptr && pEntry->m_pListEntry->LowestVcn == pAttribute->m_LowestVcn)
            pEntry->m_Attribute = pAttribute;
        else
            return E_FAIL;
    }

    return S_OK;
}

HRESULT AttributeList::DeleteAttributesForThisRecord()
{
    m_AttList.clear();
    return S_OK;
}
