//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "NtfsFileInfo.h"

#include <vector>

#pragma managed(push, off)

namespace Orc {

struct FILE_STREAM_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG StreamNameLength;
    LARGE_INTEGER StreamSize;
    LARGE_INTEGER StreamAllocationSize;
    WCHAR StreamName[1];
};
using PFILE_STREAM_INFORMATION = FILE_STREAM_INFORMATION*;

class ORCLIB_API USNRecordFileInfo : public NtfsFileInfo
{
public:
    virtual HRESULT Open();

    virtual HRESULT OpenFileFind();
    HRESULT OpenStreamInformation();

    HRESULT OpenFileInformation();

    virtual bool IsDirectory();
    virtual const std::unique_ptr<DataDetails>& GetDetails()
    {
        if (m_Details == nullptr)
            m_Details.reset(new DataDetails());
        return m_Details;
    }
    virtual std::shared_ptr<ByteStream> GetFileStream();
    virtual bool ExceedsFileThreshold(DWORD nFileSizeHigh, DWORD nFileSizeLow);

    virtual HRESULT WriteVolumeID(ITableOutput& output);

    virtual HRESULT WriteFileName(ITableOutput& output);
    virtual HRESULT WriteShortName(ITableOutput& output);
    virtual HRESULT WriteParentName(ITableOutput& output);

    virtual HRESULT WriteSizeInBytes(ITableOutput& output);

    virtual HRESULT WriteCreationDate(ITableOutput& output);
    virtual HRESULT WriteLastModificationDate(ITableOutput& output);
    virtual HRESULT WriteLastAccessDate(ITableOutput& output);

    virtual HRESULT WriteAttributes(ITableOutput& output);
    virtual HRESULT WriteUSN(ITableOutput& output);
    virtual HRESULT WriteFRN(ITableOutput& output);
    virtual HRESULT WriteParentFRN(ITableOutput& output);

    virtual HRESULT WriteADS(ITableOutput& output);
    virtual HRESULT WriteFilenameFlags(ITableOutput& output);
    virtual HRESULT WriteFilenameID(ITableOutput& output);
    virtual HRESULT WriteDataID(ITableOutput& output);

    virtual HRESULT WriteRecordInUse(ITableOutput& output);

    virtual HRESULT WriteSecDescrID(ITableOutput& output);

    USNRecordFileInfo(
        logger pLog,
        LPCWSTR szComputerName,
        const std::shared_ptr<VolumeReader>& pVolReader,
        Intentions dwDefaultIntentions,
        const std::vector<Filter>& filters,
        WCHAR* szFullFileName,
        USN_RECORD* pElt,
        Authenticode& verifytrust,
        bool bWriteErrorCodes);
    virtual ~USNRecordFileInfo(void);

private:
    USN_RECORD* m_pUSNRecord = nullptr;

    DWORD m_dwOpenStatus;
    bool m_bIsOpenStatusValid;

    bool m_bFileFindAvailable;
    WIN32_FIND_DATA m_FileFindData;

    bool m_bFileInformationAvailable;
    BY_HANDLE_FILE_INFORMATION m_fiInfo;

    bool m_bStreamInformationAvailable;
    bool m_bFileHasStreams;
    FILE_STREAM_INFORMATION* m_pFileStreamInformation;

    std::unique_ptr<DataDetails> m_Details;

    inline HRESULT CheckFileFind()
    {
        if (m_bFileFindAvailable)
            return S_OK;
        return OpenFileFind();
    }
    inline HRESULT CheckStreamInformation()
    {
        if (m_bStreamInformationAvailable)
            return S_OK;
        return OpenStreamInformation();
    }
    inline HRESULT CheckFileInformation()
    {
        if (m_bFileInformationAvailable)
            return S_OK;
        return OpenFileInformation();
    }

    virtual ULONGLONG GetFileReferenceNumber() { return m_pUSNRecord->FileReferenceNumber; }
};

}  // namespace Orc

#pragma managed(pop)
