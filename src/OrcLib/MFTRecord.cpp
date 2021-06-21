//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "MFTRecord.h"
#include "MftRecordAttribute.h"
#include "AttributeList.h"

#include "VolumeReader.h"

#include <vector>
#include <algorithm>

#include "Log/Log.h"

using namespace std;

using namespace Orc;

HRESULT MFTRecord::ParseRecord(
    const std::shared_ptr<VolumeReader>& VolReader,
    PFILE_RECORD_SEGMENT_HEADER pRecord,
    DWORD dwRecordLen,
    MFTRecord* pBaseRecord)
{
    HRESULT hr = E_FAIL;

    if (pRecord == NULL)
        return E_POINTER;

    m_pRecord = pRecord;

    if (m_pBaseFileRecord == nullptr && pBaseRecord != nullptr)
        m_pBaseFileRecord = pBaseRecord;

    if (m_pBaseFileRecord != nullptr && !m_pBaseFileRecord->IsParsed())
    {
        if (FAILED(
                hr = m_pBaseFileRecord->ParseRecord(
                    VolReader, m_pBaseFileRecord->m_pRecord, VolReader->GetBytesPerFRS(), NULL)))
        {
            Log::Debug(
                L"Skipping... Base Record could not be parsed... (FRN: {:#x}, [{}])",
                NtfsFullSegmentNumber(&(m_pBaseFileRecord->m_FileReferenceNumber)),
                SystemError(hr));
            return hr;
        }
    }
    if (IsParsed())
        return S_OK;

    if ((pRecord->MultiSectorHeader.Signature[0] != 'F') || (pRecord->MultiSectorHeader.Signature[1] != 'I')
        || (pRecord->MultiSectorHeader.Signature[2] != 'L') || (pRecord->MultiSectorHeader.Signature[3] != 'E'))
    {
        Log::Debug(
            L"Skipping... MultiSectorHeader.Signature is not FILE - \"{}{}{}{}\".",
            pRecord->MultiSectorHeader.Signature[0],
            pRecord->MultiSectorHeader.Signature[1],
            pRecord->MultiSectorHeader.Signature[2],
            pRecord->MultiSectorHeader.Signature[3]);
        return S_FALSE;
    }

    if (!m_bIsMultiSectorFixed)
    {
        if (FAILED(hr = MFTUtils::MultiSectorFixup(pRecord, VolReader)))
        {
            return hr;
        }
        m_bIsMultiSectorFixed = true;
    }

    // check if this is not a base file record segment
    if (0 != NtfsFullSegmentNumber(&(pRecord->BaseFileRecordSegment)) && pBaseRecord == NULL)
    {
        Log::Debug(
            L"Skipping... Child Record with no base File Record... yet... ({:#x})",
            NtfsFullSegmentNumber(&(pRecord->BaseFileRecordSegment)));
        m_bParsed = false;
        return S_OK;
    }

    if (pRecord->FirstAttributeOffset > dwRecordLen)
    {
        Log::Info(
            L"Skipping... Record length ({}) is smaller than First attribute Offset ({})",
            dwRecordLen,
            pRecord->FirstAttributeOffset);
        return S_FALSE;
    }

    PATTRIBUTE_RECORD_HEADER pFirstAttr = (PATTRIBUTE_RECORD_HEADER)((LPBYTE)pRecord + pRecord->FirstAttributeOffset);
    DWORD dwAllAttrLength = dwRecordLen - pRecord->FirstAttributeOffset;

    //
    // Go through each attribute
    //
    PATTRIBUTE_RECORD_HEADER pCurAttr = pFirstAttr;
    DWORD dwAttrLeftToParse = dwAllAttrLength;

    while (dwAttrLeftToParse >= sizeof(ATTRIBUTE_RECORD_HEADER))
    {

        shared_ptr<MftRecordAttribute> pNewAttr;

        if (FAILED(hr = ParseAttribute(VolReader, pCurAttr, pCurAttr->RecordLength, pNewAttr)))
            return hr;

        if (pNewAttr != nullptr)
        {
            if (m_pBaseFileRecord != NULL)
            {
                AttributeListEntry ale(pNewAttr);
                if (m_pAttributeList == nullptr)
                    m_pAttributeList = make_shared<AttributeList>();
                m_pAttributeList->m_AttList.push_back(ale);

                bool bFound = false;
                for (auto& item : m_pBaseFileRecord->m_pAttributeList->m_AttList)
                {
                    if (!bFound && item == ale)
                    {
                        _ASSERT(item.m_Attribute == NULL);
                        item.m_Attribute = pNewAttr;
                        bFound = true;
                    }
                }

                if (!bFound)
                {
                    // For some reason (like incomplete MFT and/or offline MFT, base records may miss appropriate
                    // AttrList.
                    // --> Adding entry to parent
                    m_pBaseFileRecord->m_pAttributeList->m_AttList.push_back(ale);
                }
            }
            else
            {
                AttributeListEntry ale(pNewAttr);

                bool bFound = false;
                if (m_pAttributeList == nullptr)
                    m_pAttributeList = make_shared<AttributeList>();

                for (auto& item : m_pAttributeList->m_AttList)
                {
                    if (!bFound && item == ale)
                    {
                        _ASSERT(item.m_Attribute == NULL);
                        item.m_Attribute = pNewAttr;
                        bFound = true;
                    }
                }

                if (!bFound)
                {
                    m_pAttributeList->m_AttList.push_back(ale);
                }
            }

            if (pNewAttr->m_LowestVcn != 0)
            {
                // adding continuation attribute.
                if (m_pBaseFileRecord != NULL)
                    m_pBaseFileRecord->m_pAttributeList->AddContinuationAttribute(pNewAttr);
                else
                    m_pAttributeList->AddContinuationAttribute(pNewAttr);
            }
            else
            {
                // with a LowestVcn == 0, it is very possible that continuation attributes are waiting to be added to
                // pNewAttr. checking for this condition here.

                MFTRecord* pRefRecord = (m_pBaseFileRecord != NULL) ? m_pBaseFileRecord : this;

                for (const auto& attrEntry : pRefRecord->m_pAttributeList->m_AttList)
                {
                    // without the MftRecordAttribute, no need to continue this entry
                    if (attrEntry.m_Attribute != NULL)
                    {
                        // testing if this one is 'the' one
                        if (attrEntry.m_Attribute->m_pHeader->TypeCode == pNewAttr->m_pHeader->TypeCode
                            && attrEntry.m_Attribute->m_pHeader->NameLength == pNewAttr->m_pHeader->NameLength
                            && !wcsncmp(
                                (WCHAR*)((BYTE*)attrEntry.m_Attribute->m_pHeader + attrEntry.m_Attribute->m_pHeader->NameOffset),
                                (WCHAR*)((BYTE*)pNewAttr->m_pHeader + pNewAttr->m_pHeader->NameOffset),
                                pNewAttr->m_pHeader->NameLength)
                            && attrEntry.m_Attribute->m_pHeader->FormCode == NONRESIDENT_FORM
                            && attrEntry.m_Attribute->m_pHeader->Form.Nonresident.LowestVcn != 0)
                        {
                            pNewAttr->AddContinuationAttribute(attrEntry.m_Attribute);
                        }
                    }
                }
            }
        }

        if (pCurAttr->TypeCode == $END)
            break;

        pCurAttr = (PATTRIBUTE_RECORD_HEADER)((LPBYTE)pCurAttr + pCurAttr->RecordLength);  // ready for next record
        if ((LPBYTE)pCurAttr > ((LPBYTE)pFirstAttr + dwAllAttrLength))
        {
            Log::Debug(L"Invalid MFT attribute length. Skipped");
            break;
        }

        if (pCurAttr->TypeCode == $END)
            break;

        if (pCurAttr->RecordLength == 0)
        {
            Log::Debug(L"Invalid null MFT attribute length. Skipped");
            break;
        }

        dwAttrLeftToParse -= pCurAttr->RecordLength;
    }

    m_bParsed = true;
    return S_OK;
}

PFILE_NAME MFTRecord::GetMain_PFILE_NAME() const
{
    PFILE_NAME pLeastPreferedFileName = nullptr;

    for (auto iter = m_FileNames.begin(); iter != m_FileNames.end(); ++iter)
    {
        PFILE_NAME pFile = *iter;
        if (pFile)
        {
            if (pFile->Flags == FILE_NAME_WIN32 || pFile->Flags == FILE_NAME_POSIX)
            {
                return pFile;
            }
            if (pFile->Flags & FILE_NAME_WIN32)
            {
                pLeastPreferedFileName = pFile;  //
            }
        }
    }
    if (pLeastPreferedFileName != nullptr)
    {
        return pLeastPreferedFileName;
    }
    else
    {
        // if no name was found, return last (null if none)
        return m_FileNames.empty() ? NULL : m_FileNames.back();
    }
}

HRESULT MFTRecord::ParseAttribute(
    const std::shared_ptr<VolumeReader>& VolReader,
    PATTRIBUTE_RECORD_HEADER pAttribute,
    DWORD dwAttributeLen,
    std::shared_ptr<MftRecordAttribute>& pNewAttr)
{
    HRESULT hr = E_FAIL;
    DBG_UNREFERENCED_PARAMETER(dwAttributeLen);

    pNewAttr = nullptr;

    switch (pAttribute->TypeCode)
    {
        case $STANDARD_INFORMATION: {
            Log::Debug(L"Adding $STANDARD_INFORMATION attribute");
            if (m_pStandardInformation != NULL)
            {
                Log::Error(L"More than 1 Standard Information attribute! Unexpected");
                return E_FAIL;
            }
            if (pAttribute->FormCode != RESIDENT_FORM)
            {
                Log::Error(L"Standard Information attribute is not resident! Unexpected");
                return E_FAIL;
            }
            m_pStandardInformation =
                (PSTANDARD_INFORMATION)((LPBYTE)pAttribute + pAttribute->Form.Resident.ValueOffset);

            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $ATTRIBUTE_LIST: {
            // Attribute List
            Log::Debug(L"Adding $ATTRIBUTE_LIST attribute");
            std::shared_ptr<AttributeList> NewAttributeList = make_shared<AttributeList>(pAttribute, this);

            if (SUCCEEDED(hr = NewAttributeList->ParseAttributeList(VolReader, m_FileReferenceNumber, this)))
            {
                std::for_each(
                    begin(m_pAttributeList->m_AttList),
                    end(m_pAttributeList->m_AttList),
                    [&NewAttributeList](const AttributeListEntry& entry) {
                        if (entry.Attribute() != nullptr)
                        {

                            AttributeListEntry* pEntry = NewAttributeList->FindEntryToContinue(entry.Attribute());

                            if (pEntry && pEntry->m_Attribute == nullptr)
                                pEntry->m_Attribute = entry.Attribute();
                        }
                    });

                m_pAttributeList = std::move(NewAttributeList);

                m_ChildRecords.reserve(m_pAttributeList->m_AttList.size());
                std::for_each(
                    begin(m_pAttributeList->m_AttList),
                    end(m_pAttributeList->m_AttList),
                    [this](const AttributeListEntry& entry) {
                        m_ChildRecords.push_back(
                            pair<MFTUtils::SafeMFTSegmentNumber, MFTRecord*>(entry.HostRecordSegmentNumber(), nullptr));
                    });
                std::sort(
                    begin(m_ChildRecords),
                    end(m_ChildRecords),
                    [](const pair<MFTUtils::SafeMFTSegmentNumber, MFTRecord*>& entryA,
                       const pair<MFTUtils::SafeMFTSegmentNumber, MFTRecord*>& entryB) -> bool {
                        return entryA.first < entryB.first;
                    });
                auto new_end = std::unique(
                    begin(m_ChildRecords),
                    end(m_ChildRecords),
                    [](const pair<MFTUtils::SafeMFTSegmentNumber, MFTRecord*>& entryA,
                       const pair<MFTUtils::SafeMFTSegmentNumber, MFTRecord*>& entryB) -> bool {
                        return entryA.first == entryB.first;
                    });
                m_ChildRecords.erase(new_end, end(m_ChildRecords));
                m_ChildRecords.shrink_to_fit();
                pNewAttr = std::make_shared<AttributeListAttribute>(pAttribute, this);
            }
        }
        break;

        case $FILE_NAME: {
            if (pAttribute->FormCode != RESIDENT_FORM)
            {
                Log::Error(L"FileName attribute is not resident! Unexpected");
                return E_FAIL;
            }
            if (m_pBaseFileRecord != NULL)
            {
                m_pBaseFileRecord->m_FileNames.push_back(
                    (PFILE_NAME)((LPBYTE)pAttribute + pAttribute->Form.Resident.ValueOffset));
            }
            m_FileNames.push_back((PFILE_NAME)((LPBYTE)pAttribute + pAttribute->Form.Resident.ValueOffset));
            Log::Debug(
                L"FileName: '{}', Parent: {:#x}, Flags: {:#x}",
                std::wstring_view(m_FileNames.back()->FileName, m_FileNames.back()->FileNameLength),
                NtfsFullSegmentNumber(&(m_FileNames.back()->ParentDirectory)),
                m_FileNames.back()->Flags);

            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $OBJECT_ID: {
            Log::Debug(L"Adding $OBJECT_ID attribute");
            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $SECURITY_DESCRIPTOR: {
            Log::Debug(L"Adding $SECURITY_DESCRIPTOR attribute");
            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $VOLUME_NAME: {
            Log::Debug(L"Adding $VOLUME_NAME attribute");
            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $VOLUME_INFORMATION: {
            Log::Debug(L"Adding $VOLUME_INFORMATION attribute");
            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $DATA: {
            Log::Debug(
                L"Adding $DATA attribute {}",
                pAttribute->NameLength
                    ? std::wstring_view((WCHAR*)((BYTE*)pAttribute + pAttribute->NameOffset), pAttribute->NameLength)
                    : L"$DATA");

            shared_ptr<DataAttribute> pNewDataAttr = make_shared<DataAttribute>(pAttribute, this);
            pNewAttr = pNewDataAttr;

            if (pNewAttr->m_LowestVcn == 0)
            {
                m_DataAttrList.push_back(pNewDataAttr);
                if (m_pBaseFileRecord != NULL)
                {
                    m_pBaseFileRecord->m_DataAttrList.push_back(pNewDataAttr);
                }
            }

            if (pAttribute->NameLength > 0)
                m_bHasNamedDataAttr = true;
        }
        break;
        case $INDEX_ROOT:
            // check if this is a directory
            if (pAttribute->NameLength)
            {
                if (0
                    == wcsncmp(
                        (PWSTR)((LPBYTE)pAttribute + pAttribute->NameOffset),
                        L"$I30",
                        std::max<UCHAR>(pAttribute->NameLength, 4)))
                {
                    // we need to add the file names as directories since this record is
                    // determined to be a directory.
                    if (m_pBaseFileRecord != NULL)
                    {
                        m_pBaseFileRecord->m_bIsDirectory = true;
                    }
                    m_bIsDirectory = true;

                    Log::Debug(L"Adding directory");
                    pNewAttr = make_shared<IndexRootAttribute>(pAttribute, this);
                }
                else
                {
                    Log::Debug(L"Adding $INDEX_ROOT attribute (whose name is NOT $I30)");
                    pNewAttr = make_shared<IndexRootAttribute>(pAttribute, this);
                }
            }
            else
            {
                Log::Debug(L"Adding $INDEX_ROOT attribute (no name)");
                pNewAttr = make_shared<IndexRootAttribute>(pAttribute, this);
            }

            break;
        case $INDEX_ALLOCATION: {
            if (0 == wcsncmp((PWSTR)((LPBYTE)pAttribute + pAttribute->NameOffset), L"$I30", 4))
            {
                // we need to add the file names as directories since this record is
                // determined to be a directory.
                if (m_pBaseFileRecord != NULL)
                {
                    m_pBaseFileRecord->m_bIsDirectory = true;
                }
                m_bIsDirectory = true;

                pNewAttr = make_shared<IndexAllocationAttribute>(pAttribute, this);
            }
            else
            {
                Log::Debug(L"Adding $INDEX_ROOT attribute (whose name is NOT $I30)");
                pNewAttr = make_shared<IndexAllocationAttribute>(pAttribute, this);
            }
            Log::Debug(L"Adding $INDEX_ALLOCATION attribute");
            pNewAttr = make_shared<IndexAllocationAttribute>(pAttribute, this);
        }
        break;
        case $BITMAP: {
            Log::Debug(L"Adding $BITMAP attribute");
            pNewAttr = make_shared<BitmapAttribute>(pAttribute, this);
        }
        break;
        case $REPARSE_POINT: {
            Log::Debug(L"Adding $REPARSE_POINT attribute");
            m_bHasReparsePoint = true;

            auto flags = ReparsePointAttribute::GetReparsePointType(pAttribute);

            if (ReparsePointAttribute::IsJunction(flags))
            {
                m_bIsJunction = true;
                pNewAttr = make_shared<JunctionReparseAttribute>(pAttribute, this);
            }
            else if (ReparsePointAttribute::IsSymbolicLink(flags))
            {
                m_bIsSymLink = true;
                pNewAttr = make_shared<SymlinkReparseAttribute>(pAttribute, this);
            }
            else if (ReparsePointAttribute::IsWindowsOverlayFile(flags))
            {
                std::error_code ec;
                pNewAttr = make_shared<WOFReparseAttribute>(pAttribute, this, ec);
                if (ec)
                {
                    Log::Debug("Failed to parse wof reparse point [{}]", ec);
                    return E_FAIL;
                }

                m_bIsOverlayFile = true;
            }
            else
            {
                pNewAttr = make_shared<ReparsePointAttribute>(pAttribute, this);
            }
        }
        break;
        case $EA_INFORMATION: {
            Log::Debug(L"Adding $EA_INFORMATION attribute");
            EA_INFORMATION* pEAInfo = NULL;
            if (pAttribute->FormCode == RESIDENT_FORM)
            {
                pEAInfo = (EA_INFORMATION*)(((BYTE*)pAttribute) + pAttribute->Form.Resident.ValueOffset);
            }
            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $EA: {
            Log::Debug(L"Adding $EA attribute");

            pNewAttr = make_shared<ExtendedAttribute>(pAttribute, this);
            m_bHasExtendedAttr = true;
        }
        break;
        case $LOGGED_UTILITY_STREAM: {
            Log::Debug(L"Adding $LOGGED_UTILITY_STREAM attribute");
            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
        break;
        case $END: {
            Log::Debug(L"$END attribute");
            pNewAttr = nullptr;
        }
        break;
        default: {
            Log::Warn("Unknown attribute {:#x}", pAttribute->TypeCode);
            pNewAttr = make_shared<MftRecordAttribute>(pAttribute, this);
        }
    }
    return S_OK;
}

const std::shared_ptr<DataAttribute> MFTRecord::GetDataAttribute(LPCWSTR szAttrName)
{
    std::shared_ptr<DataAttribute> retval;
    auto cbName = wcslen(szAttrName);
    auto attr_it = std::find_if(
        begin(m_DataAttrList),
        end(m_DataAttrList),
        [cbName, szAttrName](const std::shared_ptr<DataAttribute>& entry) -> bool {
            return entry->NameLength() == cbName && !_wcsnicmp(entry->NamePtr(), szAttrName, entry->NameLength());
        });
    if (attr_it == end(m_DataAttrList))
        return nullptr;

    return *attr_it;
}

const std::shared_ptr<IndexAllocationAttribute> MFTRecord::GetIndexAllocationAttribute(LPCWSTR szAttrName) const
{
    std::shared_ptr<IndexAllocationAttribute> retval;
    auto cbName = wcslen(szAttrName);
    auto attr_it = std::find_if(
        begin(m_pAttributeList->m_AttList),
        end(m_pAttributeList->m_AttList),
        [cbName, szAttrName](const AttributeListEntry& entry) -> bool {
            return entry.TypeCode() == $INDEX_ALLOCATION && entry.AttributeNameLength() == cbName
                && !_wcsnicmp(entry.AttributeName(), szAttrName, entry.AttributeNameLength());
        });
    if (attr_it == end(m_pAttributeList->m_AttList))
        return nullptr;

    return std::dynamic_pointer_cast<IndexAllocationAttribute>(attr_it->Attribute());
}
const std::shared_ptr<IndexRootAttribute> MFTRecord::GetIndexRootAttribute(LPCWSTR szAttrName) const
{
    std::shared_ptr<IndexRootAttribute> retval;
    auto cbName = wcslen(szAttrName);
    auto attr_it = std::find_if(
        begin(m_pAttributeList->m_AttList),
        end(m_pAttributeList->m_AttList),
        [cbName, szAttrName](const AttributeListEntry& entry) -> bool {
            return entry.TypeCode() == $INDEX_ROOT && entry.AttributeNameLength() == cbName
                && !_wcsnicmp(entry.AttributeName(), szAttrName, entry.AttributeNameLength());
        });
    if (attr_it == end(m_pAttributeList->m_AttList))
        return nullptr;

    return std::dynamic_pointer_cast<IndexRootAttribute>(attr_it->Attribute());
}

const std::shared_ptr<BitmapAttribute> MFTRecord::GetBitmapAttribute(LPCWSTR szAttrName) const
{
    std::shared_ptr<BitmapAttribute> retval;
    auto cbName = wcslen(szAttrName);
    auto attr_it = std::find_if(
        begin(m_pAttributeList->m_AttList),
        end(m_pAttributeList->m_AttList),
        [cbName, szAttrName](const AttributeListEntry& entry) -> bool {
            return entry.TypeCode() == $BITMAP && entry.AttributeNameLength() == cbName
                && !_wcsnicmp(entry.AttributeName(), szAttrName, entry.AttributeNameLength());
        });
    if (attr_it == end(m_pAttributeList->m_AttList))
        return nullptr;

    return std::dynamic_pointer_cast<BitmapAttribute>(attr_it->Attribute());
}

HRESULT MFTRecord::GetIndexAttributes(
    const std::shared_ptr<VolumeReader>& VolReader,
    LPCWSTR szAttrName,
    std::shared_ptr<IndexRootAttribute>& pIndexRootAttr,
    std::shared_ptr<IndexAllocationAttribute>& pIndexAllocationAttr,
    std::shared_ptr<BitmapAttribute>& pBitmapAttr) const
{
    HRESULT hr = E_FAIL;

    pIndexRootAttr = nullptr;
    pIndexAllocationAttr = nullptr;
    pBitmapAttr = nullptr;

    std::for_each(
        begin(m_pAttributeList->m_AttList),
        end(m_pAttributeList->m_AttList),
        [szAttrName, &pIndexRootAttr, &pIndexAllocationAttr, &pBitmapAttr](const AttributeListEntry& entry) {
            if (_wcsnicmp(entry.AttributeName(), szAttrName, entry.AttributeNameLength()))
                return;
            switch (entry.TypeCode())
            {
                case $INDEX_ROOT:
                    if (pIndexRootAttr == nullptr)
                        pIndexRootAttr = std::dynamic_pointer_cast<IndexRootAttribute>(entry.Attribute());
                    break;
                case $INDEX_ALLOCATION:
                    if (pIndexAllocationAttr == nullptr)
                        pIndexAllocationAttr = std::dynamic_pointer_cast<IndexAllocationAttribute>(entry.Attribute());
                    break;
                case $BITMAP:
                    if (pBitmapAttr == nullptr)
                        pBitmapAttr = std::dynamic_pointer_cast<BitmapAttribute>(entry.Attribute());
                    break;
            }
        });

    if (pBitmapAttr != nullptr)
    {
        if (FAILED(hr = pBitmapAttr->LoadBitField(VolReader)))
            return hr;
    }
    if (pIndexAllocationAttr != nullptr && pBitmapAttr != nullptr && pIndexRootAttr != nullptr)
        return S_OK;
    if (pIndexAllocationAttr == nullptr && pBitmapAttr == nullptr && pIndexRootAttr != nullptr)
        return S_OK;
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

USHORT MFTRecord::GetAttributeIndex(const std::shared_ptr<MftRecordAttribute>& one_attr) const
{
    const auto& attrs = m_pAttributeList->m_AttList;

    if (one_attr == nullptr)
        return (USHORT)-1;

    USHORT usInstanceIdx = 0L;
    for (const auto& attr : attrs)
    {
        if (attr.TypeCode() == one_attr->TypeCode())
        {
            if (attr.AttributeNameLength() == one_attr->NameLength())
            {
                if (attr.AttributeNameLength() == 0)
                    return usInstanceIdx;
                else
                {
                    if (!wcsncmp(attr.AttributeName(), one_attr->NamePtr(), one_attr->NameLength()))
                    {
                        return usInstanceIdx;
                    }
                }
            }
            usInstanceIdx++;
        }
    }
    return (USHORT)-1;
}

USHORT MFTRecord::GetFileNameIndex(const PFILE_NAME pFileName) const
{
    const auto& attrs = m_pAttributeList->m_AttList;

    USHORT usInstanceIdx = 0L;
    for (const auto& attr : attrs)
    {
        if (attr.TypeCode() == $FILE_NAME)
        {
            const auto& attribute = attr.Attribute();
            if (attribute)
            {
                auto header = attribute->Header();
                if (pFileName == (PFILE_NAME)((LPBYTE)header + header->Form.Resident.ValueOffset))
                {
                    return usInstanceIdx;
                }
            }
            usInstanceIdx++;
        }
    }
    return (USHORT)-1;
}

USHORT MFTRecord::GetDataIndex(const std::shared_ptr<DataAttribute>& pDataAttr) const
{
    return GetAttributeIndex(pDataAttr);
}

HRESULT MFTRecord::EnumData(
    const std::shared_ptr<VolumeReader>& VolReader,
    const std::shared_ptr<MftRecordAttribute>& pAttr,
    ULONGLONG ullStartOffset,
    ULONGLONG ullBytesToRead,
    MFTRecordEnumDataCallBack pCallBack)
{
    HRESULT hr = E_FAIL;

    if (VolReader == NULL)
        return E_POINTER;

    if (pAttr == NULL)
        return HRESULT_FROM_WIN32(ERROR_NO_DATA);

    PATTRIBUTE_RECORD_HEADER pHeader = pAttr->m_pHeader;

    if (pHeader->FormCode == RESIDENT_FORM)
    {
        if (ullStartOffset < pHeader->Form.Resident.ValueLength)
        {
            ullBytesToRead = min(ullBytesToRead, pHeader->Form.Resident.ValueLength - ullStartOffset);

            CBinaryBuffer Data;
            Data.SetCount(static_cast<rsize_t>(ullBytesToRead));
            CopyMemory(
                Data.GetData(),
                ((BYTE*)pHeader) + pHeader->Form.Resident.ValueOffset,
                static_cast<rsize_t>(ullBytesToRead));

            hr = pCallBack(0, Data);
        }
    }
    else
    {
        std::vector<MFTUtils::DataSegment> SegmentArray;

        if (FAILED(
                hr = pAttr->GetNonResidentSegmentsToRead(
                    VolReader, ullStartOffset, ullBytesToRead, DEFAULT_READ_SIZE, SegmentArray)))
            return hr;

        ULONGLONG ullBufferBasedOffset = 0;

        for (auto iter = SegmentArray.begin(); iter != SegmentArray.end(); ++iter)
        {
            CBinaryBuffer buffer;

            buffer.SetCount(static_cast<size_t>(iter->ullSize));
            if (iter->bUnallocated)
            {
                ZeroMemory(buffer.GetData(), static_cast<size_t>(iter->ullSize));
            }
            else
            {
                ULONGLONG ullBytesRead = 0LL;
                if (FAILED(hr = VolReader->Read(iter->ullDiskBasedOffset, buffer, iter->ullSize, ullBytesRead)))
                    return hr;
            }
            if (FAILED(hr = pCallBack(ullBufferBasedOffset, buffer)))
                return hr;
        }
    }
    return S_OK;
}

HRESULT MFTRecord::EnumData(
    const std::shared_ptr<VolumeReader>& VolReader,
    const std::shared_ptr<MftRecordAttribute>& pAttr,
    ULONGLONG ullStartOffset,
    ULONGLONG ullBytesToRead,
    ULONGLONG ullBytesChunks,
    MFTRecordEnumDataCallBack pCallBack)
{
    HRESULT hr = E_FAIL;

    if (VolReader == NULL)
        return E_POINTER;

    if (pAttr == NULL)
        return HRESULT_FROM_WIN32(ERROR_NO_DATA);

    PATTRIBUTE_RECORD_HEADER pHeader = pAttr->m_pHeader;

    if (pHeader->FormCode == RESIDENT_FORM)
    {
        if (ullStartOffset < pHeader->Form.Resident.ValueLength)
        {
            ullBytesToRead = min(ullBytesToRead, pHeader->Form.Resident.ValueLength - ullStartOffset);

            CBinaryBuffer Data;
            Data.SetCount(static_cast<rsize_t>(ullBytesToRead));
            CopyMemory(
                Data.GetData(),
                ((BYTE*)pHeader) + pHeader->Form.Resident.ValueOffset,
                static_cast<DWORD>(ullBytesToRead));

            hr = pCallBack(0, Data);
        }
    }
    else
    {
        std::vector<MFTUtils::DataSegment> SegmentArray;

        if (FAILED(
                hr = pAttr->GetNonResidentSegmentsToRead(
                    VolReader, ullStartOffset, ullBytesToRead, ullBytesChunks, SegmentArray)))
            return hr;

        ULONGLONG ullBufferBasedOffset = 0;

        for (auto iter = SegmentArray.begin(); iter != SegmentArray.end(); ++iter)
        {
            CBinaryBuffer buffer;

            buffer.SetCount(static_cast<size_t>(iter->ullSize));
            if (iter->bUnallocated)
            {
                ZeroMemory(buffer.GetData(), static_cast<size_t>(iter->ullSize));
            }
            else
            {
                ULONGLONG ullBytesRead = 0LL;
                if (FAILED(hr = VolReader->Read(iter->ullDiskBasedOffset, buffer, iter->ullSize, ullBytesRead)))
                    return hr;
            }
            if (FAILED(hr = pCallBack(ullBufferBasedOffset, buffer)))
                return hr;
        }
    }
    return S_OK;
}

HRESULT MFTRecord::ReadData(
    const std::shared_ptr<VolumeReader>& VolReader,
    const std::shared_ptr<MftRecordAttribute>& pAttr,
    ULONGLONG ullStartOffset,
    ULONGLONG ullBytesToRead,
    CBinaryBuffer& AllData,
    ULONGLONG* pullBytesRead)
{
    HRESULT hr = E_FAIL;

    ULONGLONG ullBytesRead = 0ULL;

    hr = EnumData(
        VolReader,
        pAttr,
        ullStartOffset,
        ullBytesToRead,
        [&AllData, &ullBytesRead](ULONGLONG ullBufferStartOffset, CBinaryBuffer& Data) {
            DBG_UNREFERENCED_PARAMETER(ullBufferStartOffset);
            // The end of the buffer is past our area of interest, we still need data
            ULONGLONG ullPrevBufSize = (DWORD)AllData.GetCount();
            if (!AllData.SetCount(static_cast<DWORD>(ullPrevBufSize) + Data.GetCount()))
                return E_OUTOFMEMORY;

            const PBYTE pCur = AllData.GetData() + ullPrevBufSize;

            CopyMemory(pCur, Data.GetData(), Data.GetCount());
            ullBytesRead += (DWORD)Data.GetCount();

            return S_OK;
        });

    if (pullBytesRead != nullptr)
        *pullBytesRead = ullBytesRead;
    return hr;
}

HRESULT MFTRecord::CleanCachedData()
{
    // base records own these attributes, they are responsible for their deletion, not the child records
    if (!m_DataAttrList.empty())
    {
        for (const auto& pItem : m_DataAttrList)
            pItem->CleanCachedData();
    }

    m_DataAttrList.clear();
    return S_OK;
}

HRESULT MFTRecord::CleanAttributeList()
{
    if (m_pAttributeList != nullptr)
        m_pAttributeList->DeleteAttributesForThisRecord();
    m_pAttributeList.reset();
    return S_OK;
}
