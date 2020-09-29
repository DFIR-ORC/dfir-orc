//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <memory>

#include "NtfsFileInfo.h"
#include "NtfsDataStructures.h"

#include "MftRecordAttribute.h"
#include "MftRecord.h"

#pragma managed(push, off)

namespace Orc {

class VolumeReader;

class ORCLIB_API MFTRecordFileInfo : public NtfsFileInfo
{

public:
    virtual bool IsDirectory();
    virtual std::shared_ptr<ByteStream> GetFileStream();
    virtual const std::unique_ptr<DataDetails>& GetDetails()
    {
        static std::unique_ptr<DataDetails> null;
        if (m_pDataAttr == nullptr)
            return null;
        return m_pDataAttr->GetDetails();
    }
    virtual bool ExceedsFileThreshold(DWORD nFileSizeHigh, DWORD nFileSizeLow);

    virtual HRESULT WriteFileName(ITableOutput& output);
    virtual HRESULT WriteShortName(ITableOutput& output);

    virtual HRESULT WriteAttributes(ITableOutput& output);
    virtual HRESULT WriteSizeInBytes(ITableOutput& output);
    virtual HRESULT WriteCreationDate(ITableOutput& output);
    virtual HRESULT WriteLastModificationDate(ITableOutput& output);
    virtual HRESULT WriteLastAttrChangeDate(ITableOutput& output);
    virtual HRESULT WriteLastAccessDate(ITableOutput& output);

    virtual HRESULT WriteOwnerId(ITableOutput& output);

    virtual HRESULT WriteUSN(ITableOutput& output);
    virtual HRESULT WriteFRN(ITableOutput& output);
    virtual HRESULT WriteParentFRN(ITableOutput& output);

    virtual HRESULT WriteADS(ITableOutput& output);

    virtual HRESULT WriteFilenameFlags(ITableOutput& output);

    virtual HRESULT WriteFilenameID(ITableOutput& output);
    virtual HRESULT WriteDataID(ITableOutput& output);

    virtual HRESULT WriteRecordInUse(ITableOutput& output);

    virtual HRESULT WriteFileNameLastModificationDate(ITableOutput& output);
    virtual HRESULT WriteFileNameLastAttrModificationDate(ITableOutput& output);
    virtual HRESULT WriteFileNameCreationDate(ITableOutput& output);
    virtual HRESULT WriteFileNameLastAccessDate(ITableOutput& output);

    virtual HRESULT WriteExtendedAttributes(ITableOutput& output);

    virtual HRESULT WriteSecDescrID(ITableOutput& output);

    virtual HRESULT WriteEASize(ITableOutput& output);

    virtual HRESULT WriteFilenameIndex(ITableOutput& output);
    virtual HRESULT WriteDataIndex(ITableOutput& output);

    virtual HRESULT WriteSnapshotID(ITableOutput& output);

    MFTRecordFileInfo(
        std::wstring strComputerName,
        const std::shared_ptr<VolumeReader>& pVolReader,
        Intentions dwDefaultIntentions,
        const std::vector<Filter>& filters,
        LPCWSTR szFullFileName,
        MFTRecord* pRecord,
        const PFILE_NAME pFileName,
        const std::shared_ptr<DataAttribute>& pDataAttr,
        Authenticode& verifytrust);
    virtual ~MFTRecordFileInfo(void);

private:
    MFTRecord* m_pMFTRecord = nullptr;
    PFILE_NAME m_pFileName = nullptr;
    std::shared_ptr<DataAttribute> m_pDataAttr;

    virtual HRESULT Open();

    virtual ULONGLONG GetFileReferenceNumber()
    {
        if (m_pMFTRecord == nullptr)
            return 0LL;
        return m_pMFTRecord->GetSafeMFTSegmentNumber();
    };
};

}  // namespace Orc

#pragma managed(pop)
