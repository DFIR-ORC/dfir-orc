//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "NtfsDataStructures.h"

#include "VolumeReader.h"
#include "DataDetails.h"
#include "MFTUtils.h"
#include "CryptoHashStream.h"

#include <vector>
#include <boost/dynamic_bitset/dynamic_bitset.hpp>

#pragma managed(push, off)

namespace Orc {

class MFTRecord;
class AttributeList;
class AttributeListEntry;

class ORCLIB_API MftRecordAttribute : public std::enable_shared_from_this<MftRecordAttribute>
{
    friend class MFTWalker;
    friend class MFTRecord;
    friend class AttributeList;
    friend class AttributeListEntry;

protected:
    MFTRecord* m_pHostRecord;
    PATTRIBUTE_RECORD_HEADER m_pHeader;

    std::unique_ptr<MFTUtils::NonResidentDataAttrInfo> m_pNonResidentInfo;

    std::weak_ptr<MftRecordAttribute> m_pContinuationAttribute;
    std::unique_ptr<DataDetails> m_Details;
    bool m_bNonResidentInfoPresent;
    LONGLONG m_LowestVcn;

public:
    MftRecordAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pHostingRecord)
        : m_pHeader(pHeader)
        , m_pHostRecord(pHostingRecord)
        , m_bNonResidentInfoPresent(false)
        , m_LowestVcn(0)
    {
        if (pHeader != NULL)
            if (pHeader->FormCode == NONRESIDENT_FORM)
            {
                if (pHeader->Form.Nonresident.LowestVcn)
                    m_LowestVcn = pHeader->Form.Nonresident.LowestVcn;
            }
    }

    MftRecordAttribute()
    {
        m_pHostRecord = NULL;
        m_pHeader = NULL;
        m_bNonResidentInfoPresent = false;
        m_pNonResidentInfo = NULL;
        m_LowestVcn = 0;
        m_pContinuationAttribute.reset();
    }
    bool operator==(const MftRecordAttribute&) = delete;

    PATTRIBUTE_RECORD_HEADER Header() const { return m_pHeader; };

    ATTRIBUTE_TYPE_CODE TypeCode() const
    {
        if (m_pHeader)
            return m_pHeader->TypeCode;
        return $UNUSED;
    }

    UCHAR NameLength() const { return m_pHeader != NULL ? m_pHeader->NameLength : 0; };
    WCHAR* NamePtr() const { return m_pHeader != NULL ? (WCHAR*)(((BYTE*)m_pHeader) + m_pHeader->NameOffset) : NULL; };

    bool NameEquals(LPCWSTR szAttrName)
    {
        size_t other_namelen = wcslen(szAttrName);
        size_t my_nnamelen = NameLength();

        if (other_namelen != my_nnamelen)
            return false;
        else
            return !wcsncmp(NamePtr(), szAttrName, std::max(my_nnamelen, other_namelen));
    }

    bool IsResident() const
    {
        if (m_pHeader == nullptr)
            return true;
        if (m_pHeader->FormCode == RESIDENT_FORM)
            return true;
        return false;
    };

    bool IsNonResident() const
    {
        if (m_pHeader == nullptr)
            return true;
        if (m_pHeader->FormCode == NONRESIDENT_FORM)
            return true;
        return false;
    };

    MFTUtils::NonResidentDataAttrInfo* GetNonResidentInformation(const std::shared_ptr<VolumeReader>& pVolReader);

    bool IsContinuation() const
    {
        if (m_pHeader->FormCode == RESIDENT_FORM)
            return false;
        if (m_pHeader->Form.Nonresident.LowestVcn == 0)
            return false;
        return true;
    };

    HRESULT GetNonResidentSegmentsToRead(
        const std::shared_ptr<VolumeReader>& VolReader,
        ULONGLONG ullStartOffset,
        ULONGLONG ullBytesToRead,
        ULONGLONG ullBlockSize,
        std::vector<MFTUtils::DataSegment>& ListOfSegments);

    // Size Information
    HRESULT DataSize(const std::shared_ptr<VolumeReader>& pVolReader, DWORDLONG& DataSize);
    HRESULT AllocatedSize(const std::shared_ptr<VolumeReader>& pVolReader, DWORDLONG& AllocSize);

    const std::unique_ptr<DataDetails>& GetDetails()
    {
        if (m_Details == nullptr)
            m_Details.reset(new DataDetails());
        return m_Details;
    }

    HRESULT GetStreams(
        const logger& pLog,
        const std::shared_ptr<VolumeReader>& pVolReader,
        std::shared_ptr<ByteStream>& rawStream,
        std::shared_ptr<ByteStream>& dataStream);
    HRESULT GetStreams(const logger& pLog, const std::shared_ptr<VolumeReader>& pVolReader);

    std::shared_ptr<ByteStream> GetDataStream(const logger& pLog, const std::shared_ptr<VolumeReader>& pVolReader);
    std::shared_ptr<ByteStream> GetRawStream(const logger& pLog, const std::shared_ptr<VolumeReader>& pVolReader);

    HRESULT GetHashInformation(
        const logger& pLog,
        const std::shared_ptr<VolumeReader>& pVolReader,
        SupportedAlgorithm required);

    HRESULT AddContinuationAttribute(const std::shared_ptr<MftRecordAttribute>& pMftRecordAttribute);

    virtual HRESULT CleanCachedData();

    virtual ~MftRecordAttribute() { m_pNonResidentInfo.reset(); };
};

class ORCLIB_API AttributeListAttribute : public MftRecordAttribute
{
    friend class MFTRecord;

private:
    AttributeListAttribute() = delete;
    bool operator==(const AttributeListAttribute&) const = delete;

public:
    AttributeListAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord) {};

    virtual ~AttributeListAttribute() { CleanCachedData(); }
};

class ORCLIB_API DataAttribute : public MftRecordAttribute
{
    friend class MFTRecord;

private:
    DataAttribute() = delete;
    bool operator==(const DataAttribute&) const = delete;

public:
    DataAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord) {};

    virtual ~DataAttribute() { CleanCachedData(); }
};

class ORCLIB_API IndexRootAttribute : public MftRecordAttribute
{
    friend class MFTRecord;

private:
    PINDEX_ROOT m_pRoot;

    IndexRootAttribute() = delete;
    bool operator==(const IndexRootAttribute&) const = delete;

public:
    IndexRootAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord)
    {
        if (m_pHeader->FormCode == RESIDENT_FORM)
        {
            m_pRoot = (PINDEX_ROOT)(((PBYTE)m_pHeader) + m_pHeader->Form.Resident.ValueOffset);
        }
        else
            m_pRoot = nullptr;
    };

    virtual HRESULT Open(const logger& pLog);

    ATTRIBUTE_TYPE_CODE IndexedAttributeType()
    {
        if (m_pRoot != nullptr)
            return m_pRoot->IndexedAttributeType;
        return $UNUSED;
    };

    ULONG SizePerIndex()
    {
        if (m_pRoot != nullptr)
            return m_pRoot->BytesPerIndexBuffer;
        return 0L;
    }
    ULONG BlocksPerIndex()
    {
        if (m_pRoot != nullptr)
            return m_pRoot->BlocksPerIndexBuffer;
        return 0L;
    }

    PINDEX_ENTRY FirstIndexEntry() { return (PINDEX_ENTRY)NtfsFirstIndexEntry(&m_pRoot->IndexHeader); }

    LPBYTE FirstFreeByte()
    {
        return ((LPBYTE)NtfsFirstIndexEntry(&m_pRoot->IndexHeader)) + m_pRoot->IndexHeader.FirstFreeByte;
    }
    virtual ~IndexRootAttribute() { CleanCachedData(); }
};

class ORCLIB_API IndexAllocationAttribute : public MftRecordAttribute
{
    friend class MFTRecord;

private:
    IndexAllocationAttribute() = delete;
    bool operator==(const IndexAllocationAttribute&) const = delete;

public:
    IndexAllocationAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord) {};

    virtual ~IndexAllocationAttribute() { CleanCachedData(); }
};

class ORCLIB_API BitmapAttribute : public MftRecordAttribute
{
    friend class MFTRecord;

private:
    boost::dynamic_bitset<size_t> m_bitset;

    BitmapAttribute() = delete;
    bool operator==(const IndexRootAttribute&) const = delete;

public:
    BitmapAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord) {

        };

    HRESULT LoadBitField(const std::shared_ptr<VolumeReader>& volreader);

    bool operator[](size_t index) { return m_bitset[index]; }

    const boost::dynamic_bitset<size_t>& Bits() const { return m_bitset; }

    virtual ~BitmapAttribute() { CleanCachedData(); }
};

class ORCLIB_API ReparsePointAttribute : public MftRecordAttribute
{
    friend class MFTRecord;

private:
    REPARSE_POINT_TYPE_AND_FLAGS m_Flags;
    std::wstring strSubstituteName;
    std::wstring strPrintName;

public:
    ReparsePointAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord) {};

    const std::wstring PrintName() const { return strPrintName; }
    const std::wstring SubstituteName() const { return strSubstituteName; }

    static REPARSE_POINT_TYPE_AND_FLAGS GetReparsePointType(PATTRIBUTE_RECORD_HEADER pHeader);

    static bool IsJunction(REPARSE_POINT_TYPE_AND_FLAGS flags) { return flags == 0xA0000003; }
    static bool IsSymbolicLink(REPARSE_POINT_TYPE_AND_FLAGS flags) { return flags == 0xA000000C; }
    static bool IsWindowsOverlayFile(REPARSE_POINT_TYPE_AND_FLAGS flags) { return flags == 0x80000017; }

    REPARSE_POINT_TYPE_AND_FLAGS Flags() const { return m_Flags; }

    HRESULT Parse();
    HRESULT CleanCachedData();
    virtual ~ReparsePointAttribute() { CleanCachedData(); }
};

class ORCLIB_API JunctionReparseAttribute : public ReparsePointAttribute
{
public:
    JunctionReparseAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : ReparsePointAttribute(pHeader, pRecord) {};
};

class ORCLIB_API SymlinkReparseAttribute : public ReparsePointAttribute
{
public:
    SymlinkReparseAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : ReparsePointAttribute(pHeader, pRecord) {};
};

class ORCLIB_API WOFReparseAttribute : public ReparsePointAttribute
{
public:
    WOFReparseAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : ReparsePointAttribute(pHeader, pRecord) {};
};

class ORCLIB_API ExtendedAttribute : public MftRecordAttribute
{
    friend class MFTRecord;

public:
    typedef std::pair<std::wstring, CBinaryBuffer> Item;

private:
    CBinaryBuffer m_RawData;

    bool m_bParsed;
    std::vector<Item> m_Items;

    ExtendedAttribute() = delete;
    bool operator==(const ExtendedAttribute&) const = delete;

public:
    ExtendedAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord)
        : MftRecordAttribute(pHeader, pRecord)
        , m_bParsed(false) {};

    PFILE_FULL_EA_INFORMATION EAInformation() { return (PFILE_FULL_EA_INFORMATION)m_RawData.GetData(); }
    size_t EASize() { return m_RawData.GetCount(); }

    HRESULT ReadData(const std::shared_ptr<VolumeReader>& VolReader);

    HRESULT Parse(const std::shared_ptr<VolumeReader>& VolReader);

    const std::vector<std::pair<std::wstring, CBinaryBuffer>>& Items() const { return m_Items; };

    HRESULT CleanCachedData();
    virtual ~ExtendedAttribute() { CleanCachedData(); }
};

}  // namespace Orc
#pragma managed(pop)
