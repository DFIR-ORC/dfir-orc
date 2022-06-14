//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <WinError.h>

#include "OrcLib.h"

#include "MftRecordAttribute.h"
#include "AttributeList.h"
#include "NtfsDataStructures.h"

#include "VolumeReader.h"

#include <vector>

#pragma managed(push, off)

namespace Orc {

class MFTWalker;
class MFTRecordFileInfo;

typedef HRESULT(MFTRecordEnumDataCallBack)(ULONGLONG ullBufferStartOffset, CBinaryBuffer& Data, PVOID pContext);

class MFTRecord
{
    friend class MFTWalker;
    friend class MFTRecordFileInfo;
    friend class AttributeList;
    friend class MFTOnline;

public:
    const std::vector<std::pair<MFTUtils::SafeMFTSegmentNumber, MFTRecord*>>& GetChildRecords() const
    {
        return m_ChildRecords;
    };

    bool IsDirectory() const { return m_bIsDirectory; };
    bool HasReparsePoint() const { return m_bHasReparsePoint; };
    bool IsJunction() const { return m_bIsJunction; }
    bool IsSymbolicLink() const { return m_bIsSymLink; }
    bool IsOverlayFile() const { return m_bIsOverlayFile; }

    bool IsParsed() const { return m_bParsed; };
    bool IsRecordInUse() const { return m_pRecord != nullptr ? m_pRecord->Flags & FILE_RECORD_SEGMENT_IN_USE : false; };
    bool IsBaseRecord() const
    {
        if (m_pRecord != nullptr)
        {
            if (NtfsFullSegmentNumber(&m_pRecord->BaseFileRecordSegment) != 0LL)
                return false;
            return true;
        }
        return false;
    }
    void CallbackCalled() { m_bCallbackCalled = true; };
    bool HasCallbackBeenCalled() { return m_bCallbackCalled; };

    bool HasNamedDataAttr() const { return m_bHasNamedDataAttr; };
    bool HasExtendedAttr() const { return m_bHasExtendedAttr; };

    PFILE_NAME GetDefaultFileName() const { return GetMain_PFILE_NAME(); };

    const MFTRecord* GetFileBaseRecord() const { return m_pBaseFileRecord; }

    PSTANDARD_INFORMATION GetStandardInformation() const { return m_pStandardInformation; };

    const FILE_REFERENCE& GetFileReferenceNumber() const { return m_FileReferenceNumber; };
    MFTUtils::SafeMFTSegmentNumber GetSafeMFTSegmentNumber() const
    {
        return NtfsFullSegmentNumber(&m_FileReferenceNumber);
    }
    MFTUtils::UnSafeMFTSegmentNumber GetUnSafeMFTSegmentNumber() const
    {
        return NtfsUnsafeSegmentNumber(&m_FileReferenceNumber);
    }

    const std::vector<PFILE_NAME>& GetFileNames() const { return m_FileNames; };
    const std::vector<AttributeListEntry>& GetAttributeList() const { return m_pAttributeList->m_AttList; };

    USHORT GetAttributeIndex(const std::shared_ptr<MftRecordAttribute>& attr) const;
    USHORT GetFileNameIndex(const PFILE_NAME pFileName) const;
    USHORT GetDataIndex(const std::shared_ptr<DataAttribute>& pDataAttr) const;

    const std::vector<std::shared_ptr<DataAttribute>>& GetDataAttributes() const { return m_DataAttrList; };
    const std::shared_ptr<DataAttribute> GetDataAttribute(LPCWSTR szAttrName) const;

    const std::shared_ptr<IndexAllocationAttribute> GetIndexAllocationAttribute(LPCWSTR szAttrName) const;
    const std::shared_ptr<IndexRootAttribute> GetIndexRootAttribute(LPCWSTR szAttrName) const;
    const std::shared_ptr<BitmapAttribute> GetBitmapAttribute(LPCWSTR szAttrName) const;

    HRESULT GetIndexAttributes(
        const std::shared_ptr<VolumeReader>& VolReader,
        LPCWSTR szIndexName,
        std::shared_ptr<IndexRootAttribute>& pIndexRootAttr,
        std::shared_ptr<IndexAllocationAttribute>& pIndexAllocationAttr,
        std::shared_ptr<BitmapAttribute>& pBitmapAttr) const;

    typedef std::function<HRESULT(ULONGLONG ullBufferStartOffset, CBinaryBuffer& Data)> MFTRecordEnumDataCallBack;

    HRESULT EnumData(
        const std::shared_ptr<VolumeReader>& VolReader,
        const std::shared_ptr<MftRecordAttribute>& pDataAttr,
        ULONGLONG ullStartOffset,
        ULONGLONG ullBytesToRead,
        MFTRecordEnumDataCallBack pCallBack);
    HRESULT EnumData(
        const std::shared_ptr<VolumeReader>& VolReader,
        const std::shared_ptr<MftRecordAttribute>& pDataAttr,
        ULONGLONG ullStartOffset,
        ULONGLONG ullBytesToRead,
        ULONGLONG ullBytesChunks,
        MFTRecordEnumDataCallBack pCallBack);

    HRESULT ReadData(
        const std::shared_ptr<VolumeReader>& VolReader,
        const std::shared_ptr<MftRecordAttribute>& pDataAttr,
        ULONGLONG ullStartOffset,
        ULONGLONG ullBytesToRead,
        CBinaryBuffer& Data,
        ULONGLONG* pullBytesRead);

    HRESULT CleanCachedData();
    HRESULT CleanAttributeList();

    ~MFTRecord()
    {
        CleanAttributeList();
        CleanCachedData();
        m_pRecord = NULL;
        m_pStandardInformation = NULL;
        NtfsSetSegmentNumber(&m_FileReferenceNumber, 0L, 0L);
        m_bIsDirectory = false;
        m_bParsed = false;
        m_bIsComplete = false;
        m_bCallbackCalled = false;
        m_bIsMultiSectorFixed = false;
        m_bHasNamedDataAttr = false;
        m_bHasExtendedAttr = false;
        m_pBaseFileRecord = NULL;
    }

private:
    static HCRYPTPROV g_hProv;
    bool m_bParsed = false;
    bool m_bIsComplete = false;
    bool m_bCallbackCalled = false;
    bool m_bIsDirectory = false;
    bool m_bHasNamedDataAttr = false;
    bool m_bHasExtendedAttr = false;
    bool m_bIsMultiSectorFixed = false;
    bool m_bHasReparsePoint = false;
    bool m_bIsSymLink = false;
    bool m_bIsJunction = false;
    bool m_bIsOverlayFile = false;

    FILE_REFERENCE m_FileReferenceNumber;

    // Interpreted	attributes
    std::vector<PFILE_NAME> m_FileNames;
    std::vector<std::shared_ptr<DataAttribute>> m_DataAttrList;
    std::shared_ptr<AttributeList> m_pAttributeList;

    std::vector<std::pair<MFTUtils::SafeMFTSegmentNumber, MFTRecord*>> m_ChildRecords;

    PSTANDARD_INFORMATION m_pStandardInformation = NULL;

    PFILE_RECORD_SEGMENT_HEADER m_pRecord = NULL;

    MFTRecord* m_pBaseFileRecord = NULL;

    PFILE_NAME GetMain_PFILE_NAME() const;

    HRESULT ParseAttribute(
        const std::shared_ptr<VolumeReader>& VolReader,
        PATTRIBUTE_RECORD_HEADER pAttribute,
        DWORD dwAttributeLen,
        std::shared_ptr<MftRecordAttribute>& pNewAttr);

    HRESULT ParseRecord(
        const std::shared_ptr<VolumeReader>& VolReader,
        PFILE_RECORD_SEGMENT_HEADER pRecord,
        DWORD dwRecordLen,
        MFTRecord* pBaseRecord);

    MFTRecord(const MFTRecord&) = delete;
    MFTRecord(const MFTRecord&&) noexcept = delete;
    MFTRecord& operator=(MFTRecord const&) = delete;

    MFTRecord() { NtfsSetSegmentNumber(&m_FileReferenceNumber, 0L, 0L); };
};

}  // namespace Orc

#pragma managed(pop)
