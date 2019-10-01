//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "LogFileWriter.h"
#include "TableOutputWriter.h"

#include "NTFSUtil.h"
#include "MFTWalker.h"

#include "VolumeReader.h"
#include "PartitionTable.h"
#include "Location.h"

#include "VolumeShadowCopies.h"

#include "MFTWalker.h"

#include "MFTOnline.h"
#include "MFTOffline.h"
#include "OfflineMFTReader.h"

#include <memory>
#include <sstream>
#include <boost\scope_exit.hpp>

using namespace std;

using namespace Orc;
using namespace Orc::Command::NTFSUtil;

HANDLE Main::OpenVolume(LPCWSTR szVolumePath)
{
    HRESULT hr = E_FAIL;
    HANDLE retval = INVALID_HANDLE_VALUE;

    retval = CreateFile(
        szVolumePath,
        GENERIC_READ,
        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0L,
        NULL);

    if (retval == INVALID_HANDLE_VALUE)
    {
        log::Error(
            _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to open volume %s\r\n", config.strVolume.c_str());
    }

    return retval;
}

HRESULT Main::GetUSNJournalConfiguration(HANDLE hVolume, DWORDLONG& MaximumSize, DWORDLONG& AllocationDelta)
{
    HRESULT hr = E_FAIL;
    if (hVolume == INVALID_HANDLE_VALUE)
        return E_INVALIDARG;

    USN_JOURNAL_DATA JournalData;
    DWORD dwBytesReturned = 0L;

    ZeroMemory(&JournalData, sizeof(JournalData));

    if (!DeviceIoControl(
            hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0L, &JournalData, sizeof(JournalData), &dwBytesReturned, NULL))
    {
        DWORD dwErr = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(dwErr);

        switch (dwErr)
        {
            case (ERROR_INVALID_FUNCTION):
                log::Info(
                    _L_,
                    L"The specified volume does not support change journals. Where supported, change journals can also "
                    L"be deleted.\r\n");
                break;
            case (ERROR_INVALID_PARAMETER):
                log::Info(_L_, L"One or more parameters is invalid.\r\n");
                break;
            case (ERROR_JOURNAL_DELETE_IN_PROGRESS):
                log::Info(
                    _L_,
                    L"An attempt is made to read from, create, delete, or modify the journal while a journal deletion "
                    L"is in process, or an attempt is made to write a USN record while a journal deletion is in "
                    L"process.\r\n");
                break;
            case (ERROR_JOURNAL_NOT_ACTIVE):
                log::Info(
                    _L_,
                    L"An attempt is made to write a USN record or to read the change journal while the journal is "
                    L"inactive.\r\n");
                break;
            default:
                log::Info(_L_, L"Error hr=0x%lx occured\r\n", hr);
        }
        return hr;
    }
    MaximumSize = JournalData.MaximumSize;
    AllocationDelta = JournalData.AllocationDelta;
    return S_OK;
}

HRESULT Main::PrintUSNJournalConfiguration(HANDLE hVolume)
{
    HRESULT hr = E_FAIL;
    DWORDLONG MaxSize = 0;
    DWORDLONG Delta = 0;

    if (SUCCEEDED(hr = GetUSNJournalConfiguration(hVolume, MaxSize, Delta)))
    {
        log::Info(_L_, L"Maximum size    : ");
        _L_->WriteSizeInUnits(MaxSize);
        log::Info(_L_, L"\r\nAllocation delta: ");
        _L_->WriteSizeInUnits(Delta);
        log::Info(_L_, L"\r\n");
    }
    else
    {
        log::Error(_L_, hr, L"Failed to retrive USN journal configuration for %s\r\n", config.strVolume.c_str());
    }
    return S_OK;
}

HRESULT Main::ConfigureUSNJournal(HANDLE hVolume)
{
    HRESULT hr = E_FAIL;

    if (hVolume == INVALID_HANDLE_VALUE)
        return E_INVALIDARG;

    if (config.dwlMinSize > 0)
    {
        DWORDLONG MaxSize = 0;
        DWORDLONG Delta = 0;

        if (SUCCEEDED(hr = GetUSNJournalConfiguration(hVolume, MaxSize, Delta)))
        {
            if (MaxSize >= config.dwlMinSize)
                return S_FALSE;
            else
                config.dwlMaxSize = config.dwlMinSize;
        }
        else
        {
            log::Error(_L_, hr, L"Failed to retrive USN journal configuration for %s\r\n", config.strVolume.c_str());
        }
    }

    CREATE_USN_JOURNAL_DATA JournalCreateData = {config.dwlMaxSize, config.dwlAllocDelta};
    DWORD dwBytesReturned = 0L;

    if (!DeviceIoControl(
            hVolume,
            FSCTL_CREATE_USN_JOURNAL,
            &JournalCreateData,
            sizeof(JournalCreateData),
            NULL,
            0L,
            &dwBytesReturned,
            NULL))
    {
        DWORD dwErr = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(dwErr);

        switch (dwErr)
        {
            case (ERROR_INVALID_FUNCTION):
                log::Error(
                    _L_,
                    hr,
                    L"The specified volume does not support change journals. Where supported, change journals can also "
                    L"be deleted.\r\n");
                break;
            case (ERROR_INVALID_PARAMETER):
                log::Error(_L_, hr, L"One or more parameters is invalid.\r\n");
                break;
            case (ERROR_JOURNAL_DELETE_IN_PROGRESS):
                log::Error(
                    _L_,
                    hr,
                    L"An attempt is made to read from, create, delete, or modify the journal while a journal deletion "
                    L"is in process, or an attempt is made to write a USN record while a journal deletion is in "
                    L"process.\r\n");
                break;
            default:
                log::Error(_L_, E_UNEXPECTED, L"Error hr=0x%lx occured\r\n", hr);
        }
        return hr;
    }
    return S_OK;
}

HRESULT Main::PrintAllLocations()
{
    HRESULT hr = E_FAIL;
    LocationSet aSet(_L_);

    // enumerate all possible locations
    if (FAILED(hr = aSet.EnumerateLocations()))
    {
        log::Error(_L_, hr, L"Failed to enumerate locations\r\n");
        return hr;
    }

    aSet.Consolidate(true, FSVBR::FSType::ALL);
    aSet.PrintLocationsByVolume(false);

    return S_OK;
}

HRESULT Main::PrintLocationDetails()
{
    HRESULT hr = E_FAIL;

    LocationSet aSet(_L_);

    Location::Type locType = Location::Undetermined;
    wstring canonical;
    wstring subdir;

    if (FAILED(hr = aSet.CanonicalizeLocation(config.strVolume.c_str(), canonical, locType, subdir)))
        return hr;

    switch (locType)
    {
        case Location::ImageFileDisk:
            log::Info(_L_, L"\r\n\r\n%s is a disk image file\r\n", config.strVolume.c_str());
            break;
        case Location::PhysicalDrive:
            log::Info(_L_, L"\r\n\r\n%s is a physical drive device\r\n", config.strVolume.c_str());
            break;
        case Location::PhysicalDriveVolume:
            log::Info(_L_, L"\r\n\r\n%s is a physical drive volume\r\n", config.strVolume.c_str());
            break;
        case Location::DiskInterface:
            log::Info(_L_, L"\r\n\r\n%s is a physical disk interface\r\n", config.strVolume.c_str());
            break;
        case Location::DiskInterfaceVolume:
            log::Info(_L_, L"\r\n\r\n%s is a physical disk interface volume\r\n", config.strVolume.c_str());
            break;
        case Location::MountedVolume:
            log::Info(_L_, L"\r\n\r\n%s is a mounted volume\r\n", config.strVolume.c_str());
            break;
        case Location::OfflineMFT:
            log::Info(_L_, L"\r\n\r\n%s is an offline $MFT\r\n", config.strVolume.c_str());
            break;
        case Location::Snapshot:
            log::Info(_L_, L"\r\n\r\n%s is a VSS Snapshot\r\n", config.strVolume.c_str());
            break;
        case Location::ImageFileVolume:
            log::Info(_L_, L"\r\n\r\n%s is a volume image file\r\n", config.strVolume.c_str());
            break;
        case Location::Undetermined:
            log::Info(_L_, L"\r\n\r\n%s could not be determined\r\n", config.strVolume.c_str());
            break;
        default:
            break;
    }

    if (locType == Location::ImageFileDisk || locType == Location::PhysicalDrive || locType == Location::DiskInterface)
    {
        PartitionTable pt(_L_);

        if (FAILED(hr = pt.LoadPartitionTable(canonical.c_str())))
        {
            log::Error(_L_, hr, L"Failed to load partition table for %s\r\n", canonical.c_str());
            return hr;
        }

        log::Info(_L_, L"\r\n\r\n%s contains the following partition table:\r\n\r\n", config.strVolume.c_str());

        std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
            std::wstringstream ss;
            ss << part;

            log::Info(_L_, L"\tPartition %s\r\n", ss.str().c_str());
        });
    }

    std::vector<std::shared_ptr<Location>> addedLocations;

    if (FAILED(hr = aSet.AddLocations(config.strVolume.c_str(), addedLocations, false)))
    {
        log::Error(_L_, hr, L"Failed to parse location %s\r\n", config.strVolume.c_str());
        return hr;
    }

    log::Info(_L_, L"\r\n\r\nLocation %s is parsed as:\r\n", config.strVolume.c_str());
    std::for_each(begin(addedLocations), end(addedLocations), [this](const std::shared_ptr<Location>& aLoc) {
        log::Info(_L_, L"\r\n");
        log::Info(_L_, L"\t%s\n", aLoc->GetIdentifier().c_str());
        log::Info(_L_, L"\r\n");

        auto reader = aLoc->GetReader();

        if (reader != nullptr)
        {
            if (aLoc->IsNTFS())
            {
                MFTWalker walker(_L_);

                if (SUCCEEDED(walker.Initialize(aLoc)))
                {
                    log::Info(_L_, L"\t\tFS type : NTFS\r\n");
                    log::Info(_L_, L"\t\tBytes per cluster : %d\r\n", reader->GetBytesPerCluster());
                    log::Info(_L_, L"\t\tBytes per FRS     : %d\r\n", reader->GetBytesPerFRS());
                    log::Info(_L_, L"\t\tVolume Serial     : 0x%I64X\r\n", reader->VolumeSerialNumber());
                    log::Info(_L_, L"\t\tComponent length  : %d\r\n", reader->MaxComponentLength());
                    log::Info(_L_, L"\t\tMFT record count : %d\r\n", walker.GetMFTRecordCount());
                    log::Info(_L_, L"\r\n");
                }
            }
            else if (aLoc->IsFAT12() || aLoc->IsFAT16() || aLoc->IsFAT32())
            {
                if (aLoc->IsFAT12())
                {
                    log::Info(_L_, L"\t\tFS type : FAT12\r\n");
                }
                else if (aLoc->IsFAT16())
                {
                    log::Info(_L_, L"\t\tFS type : FAT16\r\n");
                }
                else
                {
                    log::Info(_L_, L"\t\tFS type : FAT32\r\n");
                }

                log::Info(_L_, L"\t\tBytes per cluster : %d\r\n", reader->GetBytesPerCluster());
                log::Info(_L_, L"\t\tVolume Serial     : 0x%I64X\r\n", reader->VolumeSerialNumber());
                log::Info(_L_, L"\r\n");
            }
        }
    });

    return S_OK;
}

HRESULT Main::PrintNonResidentAttributeDetails(
    const std::shared_ptr<VolumeReader>& volReader,
    MFTRecord* pRecord,
    const std::shared_ptr<MftRecordAttribute>& pAttr,
    DWORD dwIndex)
{
    HRESULT hr = E_FAIL;

    auto pNRI = pAttr->GetNonResidentInformation(volReader);
    log::Info(_L_, L"\t[%2.2d]AllocatedSize       : 0x%.16I64X\r\n", dwIndex, pNRI->ExtentsSize);

    auto extents = pNRI->ExtentsVector;

    if (!extents.empty())
    {
        UINT i = 0L;

        log::Info(_L_, L"\r\n\t[%2.2d]Extent[%2.2d]:\r\n\r\n", dwIndex, extents.size());

        std::for_each(begin(extents), end(extents), [this, &i](const MFTUtils::NonResidentAttributeExtent& extent) {
            log::Info(
                _L_,
                L"\t\t[%2.2d]: LowestVCN=0x%.16I64X /Offset=0x%.16I64X /Size=0x%.16I64X%s\r\n",
                i,
                extent.LowestVCN,
                extent.DiskOffset,
                extent.DiskAlloc,
                extent.bZero ? (L" SPARSE") : (L""));
            i++;
        });
        log::Info(_L_, L"\r\n");
    }
    return S_OK;
}

HRESULT Main::PrintRecordDetails(const std::shared_ptr<VolumeReader>& volReader, MFTRecord* pRecord)
{
    HRESULT hr = E_FAIL;

    log::Info(
        _L_,
        L"\r\nRECORD 0x%.16I64X %s%s%s%s%s%s%s%s%s\r\n",
        pRecord->GetSafeMFTSegmentNumber(),
        pRecord->IsRecordInUse()    ? L"(in use)" : L"(deleted)",
		pRecord->IsDirectory()      ? L" (directory)" : L"",
		pRecord->IsBaseRecord()     ? L" (base)" : L" (child)",
		pRecord->IsJunction()       ? L" (junction)" : L"",
		pRecord->IsOverlayFile()    ? L" (overlay)" : L"",
		pRecord->IsSymbolicLink()   ? L" (symlink)" : L"",
		pRecord->HasExtendedAttr()  ? L" (has extended attr)" : L"",
		pRecord->HasNamedDataAttr() ? L" (has named $DATA)" : L"",
		pRecord->HasReparsePoint()  ? L" (has reparse point)" : L""
		);

	

    const auto& children = pRecord->GetChildRecords();

    if (!children.empty())
    {
        log::Info(_L_, L"\r\nCHILDREN[%2.2d]\r\n", children.size() - 1);

        DWORD idx = 0;
        for (const auto& child : children)
        {
            if (child.first != pRecord->GetSafeMFTSegmentNumber())
                log::Info(_L_, L"\tCHILD[%2.2d] : 0x%.16I64X\r\n", idx++, child.first);
        }
    }

    const auto& attrlist = pRecord->GetAttributeList();

    log::Info(_L_, L"\r\nATTRIBUTES[%2.2d]:\r\n\r\n", attrlist.size());

    for (const auto& entry : attrlist)
    {
        log::Info(
            _L_,
            L"\tID=%2.2d: Name=\"%.*s\",Type=%s,Form=%s",
            entry.Instance(),
            entry.AttributeNameLength(),
            entry.AttributeName(),
            entry.TypeStr(),
            entry.FormCode() == RESIDENT_FORM ? L"R" : L"NR");
        if (entry.LowestVCN() > 0)
        {
            log::Info(_L_, L",LowestVCN=0x%.16I64X\r\n", entry.LowestVCN());
        }
        else
        {
            log::Info(_L_, L"\r\n");
        }
    }

    log::Info(_L_, L"\r\n$STANDARD_INFORMATION:\r\n\r\n");

    log::Info(_L_, L"\tFileAttributes      : ");
    _L_->WriteAttributes(pRecord->GetStandardInformation()->FileAttributes);
    log::Info(_L_, L"\r\n\tCreationTime        : ");
    _L_->WriteFileTime(pRecord->GetStandardInformation()->CreationTime);
    log::Info(_L_, L"\r\n\tLastModificationTime: ");
    _L_->WriteFileTime(pRecord->GetStandardInformation()->LastModificationTime);
    log::Info(_L_, L"\r\n\tLastAccessTime      : ");
    _L_->WriteFileTime(pRecord->GetStandardInformation()->LastAccessTime);
    log::Info(_L_, L"\r\n\tLastChangeTime      : ");
    _L_->WriteFileTime(pRecord->GetStandardInformation()->LastChangeTime);
    log::Info(_L_, L"\r\n\tOwnerID             : %d", pRecord->GetStandardInformation()->OwnerId);
    log::Info(_L_, L"\r\n\tSecurityID          : %d", pRecord->GetStandardInformation()->SecurityId);

    auto namelist = pRecord->GetFileNames();

    if (!namelist.empty())
    {
        log::Info(_L_, L"\r\n\r\n$FILE_NAMES[%2.2d]:\r\n\r\n", namelist.size());
        UINT i = 0L;
        std::for_each(begin(namelist), end(namelist), [this, &i, pRecord](const PFILE_NAME pFileName) {
            log::Info(
                _L_, L"\t[%2.2d]FileName            : \"%.*s\"", i, pFileName->FileNameLength, pFileName->FileName);
            log::Info(_L_, L"\r\n\t[%2.2d]ParentDirectory     : ", i);
            _L_->WriteFRN(pFileName->ParentDirectory);
            log::Info(_L_, L"\r\n\t[%2.2d]CreationTime        : ", i);
            _L_->WriteFileTime(pFileName->Info.CreationTime);
            log::Info(_L_, L"\r\n\t[%2.2d]LastModificationTime: ", i);
            _L_->WriteFileTime(pFileName->Info.LastModificationTime);
            log::Info(_L_, L"\r\n\t[%2.2d]LastAccessTime      : ", i);
            _L_->WriteFileTime(pFileName->Info.LastAccessTime);
            log::Info(_L_, L"\r\n\t[%2.2d]LastChangeTime      : ", i);
            _L_->WriteFileTime(pFileName->Info.LastChangeTime);

            LPCWSTR nameflags[] = {
                L"FILE_NAME_POSIX", L"FILE_NAME_WIN32", L"FILE_NAME_DOS83", L"FILE_NAME_WIN32|FILE_NAME_DOS83", NULL};
            log::Info(_L_, L"\r\n\t[%2.2d]FileNameFlags       : ", i);
            _L_->WriteEnum(pFileName->Flags, nameflags);

            // Determine $FILE_NAME attribute ID
            const auto& attrs = pRecord->GetAttributeList();

            USHORT usInstanceID = 0L;
            std::for_each(begin(attrs), end(attrs), [this, &usInstanceID, pFileName](const AttributeListEntry& attr) {
                if (attr.TypeCode() == $FILE_NAME)
                {
                    const auto& attribute = attr.Attribute();
                    if (attribute)
                    {
                        auto header = (PATTRIBUTE_RECORD_HEADER)attribute->Header();
                        if (pFileName == (PFILE_NAME)((LPBYTE)header + header->Form.Resident.ValueOffset))
                        {
                            usInstanceID = header->Instance;
                        }
                    }
                    else
                    {
                        usInstanceID = (USHORT)-1;
                    }
                }
            });
            if (usInstanceID == (USHORT)-1)
            {
                log::Info(_L_, L"\r\n\t[%2.2d]FileNameID          : Failed to determine $FILE_NAME attribute id", i);
            }

            log::Info(_L_, L"\r\n\t[%2.2d]FileNameID          : %d\r\n\r\n", i, usInstanceID);
            i++;
        });
    }

    auto datalist = pRecord->GetDataAttributes();

    if (!datalist.empty())
    {
        log::Info(_L_, L"\r\n\r\n$DATA[%2.2d]:\r\n\r\n", datalist.size());

        UINT j = 0L;
        std::for_each(
            begin(datalist), end(datalist), [this, volReader, pRecord, &j](const std::shared_ptr<DataAttribute> pData) {
                log::Info(
                    _L_, L"\t[%2.2d]DataName            : \"%.*s\"\r\n", j, pData->NameLength(), pData->NamePtr());
                DWORDLONG ullSize = 0LL;
                if (FAILED(pData->DataSize(volReader, ullSize)))
                {
                    log::Info(_L_, L"\t[%2.2d]DataSize            : Failed\r\n", j);
                }
                else
                {
                    log::Info(_L_, L"\t[%2.2d]DataSize            : 0x%.16I64X\r\n", j, ullSize);
                }

                if (!pData->IsResident())
                {
                    PrintNonResidentAttributeDetails(volReader, pRecord, pData, j);
                }
                else
                {
                    auto pHeader = pData->Header();

                    BYTE* pData = ((BYTE*)pHeader) + pHeader->Form.Resident.ValueOffset;
                    CBinaryBuffer dataBuffer(pData, pHeader->Form.Resident.ValueLength);

                    log::Info(_L_, L"\t[%2.2d]Data                : \r\n\r\n", j);

                    dataBuffer.PrintHex(_L_, L"\t\t");
                }
                j++;
            });
        log::Info(_L_, L"\r\n");
    }

    if (pRecord->IsDirectory())
    {
        std::shared_ptr<IndexAllocationAttribute> pIA;
        std::shared_ptr<IndexRootAttribute> pIR;
        std::shared_ptr<BitmapAttribute> pBM;

        if (FAILED(hr = pRecord->GetIndexAttributes(volReader, L"$I30", pIR, pIA, pBM)))
        {
            log::Error(_L_, hr, L"Failed to find $I30 attributes\r\n");
            return hr;
        }

        if (pIR != nullptr)
        {
            log::Info(_L_, L"\r\n\r\n$INDEX_ROOT(\"%.*s\")\r\n\r\n", pIR->NameLength(), pIR->NamePtr());
            log::Info(
                _L_,
                L"\tIndexedAttributeType: %X (%s)\r\n",
                pIR->IndexedAttributeType(),
                AttributeListEntry::TypeStr(pIR->IndexedAttributeType()));
            log::Info(_L_, L"\tSizePerIndex        : %d\r\n", pIR->SizePerIndex());
            log::Info(_L_, L"\tBlocksPerIndex      : %d\r\n\r\n", pIR->BlocksPerIndex());

            PINDEX_ENTRY entry = pIR->FirstIndexEntry();

            UINT i = 0L;

            while (!(entry->Flags & INDEX_ENTRY_END))
            {
                PFILE_NAME pFileName = (PFILE_NAME)((PBYTE)entry + sizeof(INDEX_ENTRY));
                log::Info(
                    _L_,
                    L"\t[%2.2d]IndexedAttrOwner    : 0x%.16I64X\r\n",
                    i,
                    NtfsFullSegmentNumber(&entry->FileReference));
                log::Info(
                    _L_, L"\t[%2.2d]FileName            : \"%.*s\"", i, pFileName->FileNameLength, pFileName->FileName);
                log::Info(_L_, L"\r\n\t[%2.2d]ParentDirectory     : ", i);
                _L_->WriteFRN(pFileName->ParentDirectory);
                log::Info(_L_, L"\r\n\t[%2.2d]CreationTime        : ", i);
                _L_->WriteFileTime(pFileName->Info.CreationTime);
                log::Info(_L_, L"\r\n\t[%2.2d]LastModificationTime: ", i);
                _L_->WriteFileTime(pFileName->Info.LastModificationTime);
                log::Info(_L_, L"\r\n\t[%2.2d]LastAccessTime      : ", i);
                _L_->WriteFileTime(pFileName->Info.LastAccessTime);
                log::Info(_L_, L"\r\n\t[%2.2d]LastChangeTime      : ", i);
                _L_->WriteFileTime(pFileName->Info.LastChangeTime);

                LPCWSTR nameflags[] = {L"FILE_NAME_POSIX",
                                       L"FILE_NAME_WIN32",
                                       L"FILE_NAME_DOS83",
                                       L"FILE_NAME_WIN32|FILE_NAME_DOS83",
                                       NULL};
                log::Info(_L_, L"\r\n\t[%2.2d]FileNameFlags       : ", i);
                _L_->WriteEnum(pFileName->Flags, nameflags);
                log::Info(_L_, L"\r\n\r\n");

                entry = NtfsNextIndexEntry(entry);

                i++;
            }
        }

        if (pBM != nullptr)
        {
            log::Info(_L_, L"\r\n$BITMAP(\"%.*s\")\r\n\r\n", pBM->NameLength(), pBM->NamePtr());
            std::wstring strBits;
            boost::to_string(pBM->Bits(), strBits);

            log::Info(_L_, L"\tBits:%s\r\n\r\n", strBits.c_str());
        }

        if (pIA != nullptr)
        {
            log::Info(_L_, L"\r\n$INDEX_ALLOCATION(\"%.*s\")\r\n\r\n", pIA->NameLength(), pIA->NamePtr());

            auto stream = pIA->GetDataStream(_L_, volReader);

            ULONGLONG ToRead = pIR->SizePerIndex();
            ULONGLONG Read = ToRead;
            CBinaryBuffer buffer;
            buffer.SetCount(static_cast<size_t>(ToRead));
            UINT entry_count = 0L;
            UINT carved_count = 0L;
            UINT entry_block = 0L;

            if (pIA->IsNonResident())
            {
                PrintNonResidentAttributeDetails(volReader, pRecord, pIA, 0L);
            }

            while (Read > 0)
            {
                Read = 0ULL;
                if (FAILED(hr = stream->Read(buffer.GetData(), buffer.GetCount(), &Read)))
                {
                    log::Error(_L_, hr, L"Failed to read $INDEX_ALLOCATION\r\n");
                }
                else if (Read > 0)
                {
                    if ((*pBM)[entry_block])
                    {
                        log::Info(_L_, L"\r\n\tBLOCK[%3.3d]\r\n\r\n", entry_block);

                        PINDEX_ALLOCATION_BUFFER pIABuff = (PINDEX_ALLOCATION_BUFFER)buffer.GetData();

                        if (FAILED(hr = MFTUtils::MultiSectorFixup(pIABuff, pIR->SizePerIndex(), volReader)))
                        {
                            log::Error(_L_, hr, L"Failed to fixup $INDEX_ALLOCATION header\r\n");
                        }
                        else
                        {
                            PINDEX_HEADER pHeader = &(pIABuff->IndexHeader);

                            PINDEX_ENTRY pEntry = (PINDEX_ENTRY)NtfsFirstIndexEntry(pHeader);
                            while (!(pEntry->Flags & INDEX_ENTRY_END))
                            {
                                PFILE_NAME pFileName = (PFILE_NAME)((PBYTE)pEntry + sizeof(INDEX_ENTRY));
                                log::Info(
                                    _L_,
                                    L"\t[%2.2d] IndexedAttrOwner    : 0x%.16I64X\r\n",
                                    entry_count,
                                    NtfsFullSegmentNumber(&pEntry->FileReference));
                                log::Info(
                                    _L_,
                                    L"\t[%2.2d] FileName            : \"%.*s\"",
                                    entry_count,
                                    pFileName->FileNameLength,
                                    pFileName->FileName);
                                log::Info(_L_, L"\r\n\t[%2.2d] ParentDirectory     : ", entry_count);
                                _L_->WriteFRN(pFileName->ParentDirectory);
                                log::Info(_L_, L"\r\n\t[%2.2d] CreationTime        : ", entry_count);
                                _L_->WriteFileTime(pFileName->Info.CreationTime);
                                log::Info(_L_, L"\r\n\t[%2.2d] LastModificationTime: ", entry_count);
                                _L_->WriteFileTime(pFileName->Info.LastModificationTime);
                                log::Info(_L_, L"\r\n\t[%2.2d] LastAccessTime      : ", entry_count);
                                _L_->WriteFileTime(pFileName->Info.LastAccessTime);
                                log::Info(_L_, L"\r\n\t[%2.2d] LastChangeTime      : ", entry_count);
                                _L_->WriteFileTime(pFileName->Info.LastChangeTime);

                                LPCWSTR nameflags[] = {L"FILE_NAME_POSIX",
                                                       L"FILE_NAME_WIN32",
                                                       L"FILE_NAME_DOS83",
                                                       L"FILE_NAME_WIN32|FILE_NAME_DOS83",
                                                       NULL};
                                log::Info(_L_, L"\r\n\t[%2.2d] FileNameFlags       : ", entry_count);
                                _L_->WriteEnum(pFileName->Flags, nameflags);
                                log::Info(_L_, L"\r\n\r\n");

                                pEntry = NtfsNextIndexEntry(pEntry);

                                entry_count++;
                            }

                            log::Info(_L_, L"\r\n\tCarving BLOCK[%3.3d]\r\n", entry_block);

                            LPBYTE pFirstFreeByte = (((LPBYTE)NtfsFirstIndexEntry(pHeader)) + pHeader->FirstFreeByte);

                            while (pFirstFreeByte + sizeof(FILE_NAME) < buffer.GetData() + buffer.GetCount())
                            {
                                PFILE_NAME pCarvedFileName = (PFILE_NAME)pFirstFreeByte;

                                if (NtfsFullSegmentNumber(&pCarvedFileName->ParentDirectory)
                                    == pRecord->GetSafeMFTSegmentNumber())
                                {
                                    PINDEX_ENTRY pEntry = (PINDEX_ENTRY)((LPBYTE)pCarvedFileName - sizeof(INDEX_ENTRY));
                                    log::Info(_L_, L"\r\n");
                                    log::Info(
                                        _L_,
                                        L"\tX%2.2dX IndexedAttrOwner    : 0x%.16I64X\r\n",
                                        carved_count,
                                        NtfsFullSegmentNumber(&pEntry->FileReference));
                                    log::Info(
                                        _L_,
                                        L"\tX%2.2dX FileName            : \"%.*s\"",
                                        carved_count,
                                        pCarvedFileName->FileNameLength,
                                        pCarvedFileName->FileName);
                                    log::Info(_L_, L"\r\n\tX%2.2dX ParentDirectory     : ", carved_count);
                                    _L_->WriteFRN(pCarvedFileName->ParentDirectory);
                                    log::Info(_L_, L"\r\n\tX%2.2dX CreationTime        : ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.CreationTime);
                                    log::Info(_L_, L"\r\n\tX%2.2dX LastModificationTime: ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.LastModificationTime);
                                    log::Info(_L_, L"\r\n\tX%2.2dX LastAccessTime      : ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.LastAccessTime);
                                    log::Info(_L_, L"\r\n\tX%2.2dX LastChangeTime      : ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.LastChangeTime);

                                    LPCWSTR nameflags[] = {L"FILE_NAME_POSIX",
                                                           L"FILE_NAME_WIN32",
                                                           L"FILE_NAME_DOS83",
                                                           L"FILE_NAME_WIN32|FILE_NAME_DOS83",
                                                           NULL};
                                    log::Info(_L_, L"\r\n\tX%2.2dX FileNameFlags       : ", carved_count);
                                    _L_->WriteEnum(pCarvedFileName->Flags, nameflags);
                                    log::Info(_L_, L"\r\n\r\n");
                                    carved_count++;
                                }
                                pFirstFreeByte++;
                            }
                        }
                    }
                    else
                    {
                        log::Info(_L_, L"\r\n\tBLOCK[%3.3d] (unallocated)\r\n\r\n", entry_block);

                        PINDEX_ALLOCATION_BUFFER pIABuff = (PINDEX_ALLOCATION_BUFFER)buffer.GetData();

                        if (FAILED(hr = MFTUtils::MultiSectorFixup(pIABuff, pIR->SizePerIndex(), volReader)))
                        {
                            log::Verbose(_L_, L"Failed to fixup $INDEX_ALLOCATION header (carved)\r\n");
                        }
                        else
                        {
                            log::Info(_L_, L"\r\n\tCarving BLOCK[%3.3d]\r\n", entry_block);

                            LPBYTE pFirstFreeByte = (LPBYTE)pIABuff;

                            while (pFirstFreeByte + sizeof(FILE_NAME) < buffer.GetData() + buffer.GetCount())
                            {
                                PFILE_NAME pCarvedFileName = (PFILE_NAME)pFirstFreeByte;

                                if (NtfsFullSegmentNumber(&pCarvedFileName->ParentDirectory)
                                    == pRecord->GetSafeMFTSegmentNumber())
                                {
                                    PINDEX_ENTRY pEntry = (PINDEX_ENTRY)((LPBYTE)pCarvedFileName - sizeof(INDEX_ENTRY));
                                    log::Info(_L_, L"\r\n");
                                    log::Info(
                                        _L_,
                                        L"\tX%2.2dX IndexedAttrOwner    : 0x%.16I64X\r\n",
                                        carved_count,
                                        NtfsFullSegmentNumber(&pEntry->FileReference));
                                    log::Info(
                                        _L_,
                                        L"\tX%2.2dX FileName            : \"%.*s\"",
                                        carved_count,
                                        pCarvedFileName->FileNameLength,
                                        pCarvedFileName->FileName);
                                    log::Info(_L_, L"\r\n\tX%2.2dX ParentDirectory     : ", carved_count);
                                    _L_->WriteFRN(pCarvedFileName->ParentDirectory);
                                    log::Info(_L_, L"\r\n\tX%2.2dX CreationTime        : ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.CreationTime);
                                    log::Info(_L_, L"\r\n\tX%2.2dX LastModificationTime: ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.LastModificationTime);
                                    log::Info(_L_, L"\r\n\tX%2.2dX LastAccessTime      : ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.LastAccessTime);
                                    log::Info(_L_, L"\r\n\tX%2.2dX LastChangeTime      : ", carved_count);
                                    _L_->WriteFileTime(pCarvedFileName->Info.LastChangeTime);

                                    LPCWSTR nameflags[] = {L"FILE_NAME_POSIX",
                                                           L"FILE_NAME_WIN32",
                                                           L"FILE_NAME_DOS83",
                                                           L"FILE_NAME_WIN32|FILE_NAME_DOS83",
                                                           NULL};
                                    log::Info(_L_, L"\r\n\tX%2.2dX FileNameFlags       : ", carved_count);
                                    _L_->WriteEnum(pCarvedFileName->Flags, nameflags);
                                    log::Info(_L_, L"\r\n\r\n");
                                    carved_count++;
                                }
                                pFirstFreeByte++;
                            }
                        }
                    }
                }
                entry_block++;
            }
            log::Info(
                _L_,
                L"\r\n\t$INDEX_ALLOCATION statistics: Blocks=%d, Entries=%d, Carved=%d\r\n\r\n",
                entry_block,
                entry_count,
                carved_count);
        }
    }

    log::Info(_L_, L"\r\n");

    if (pRecord->HasExtendedAttr())
    {
        for (const auto& attr : attrlist)
        {
            if (attr.TypeCode() == $EA)
            {
                log::Info(_L_, L"\r\n$EA Name=\"%.*s\"\r\n\r\n", attr.AttributeNameLength(), attr.AttributeName());

                auto ea_attr = std::dynamic_pointer_cast<ExtendedAttribute, MftRecordAttribute>(attr.Attribute());
                if (ea_attr == nullptr)
                    continue;

                if (FAILED(ea_attr->Parse(volReader)))
                    continue;

                for (auto& ea : ea_attr->Items())
                {

                    log::Info(
                        _L_, L"\tName=%s\n\tDataSize=%d\r\n\tData:\r\n", ea.first.c_str(), (DWORD)ea.second.GetCount());
                    ea.second.PrintHex(_L_, L"\t\t");
                }
            }
        };
    }

    if (pRecord->HasReparsePoint())
    {
        if (pRecord->IsJunction())
        {
            log::Info(_L_, L"\r\nJUNCTION\r\n\r\n");
        }
        if (pRecord->IsSymbolicLink())
        {
            log::Info(_L_, L"\r\nSYMBOLIC LINK\r\n\r\n");
        }
        if (pRecord->IsOverlayFile())
        {
            log::Info(_L_, L"\r\nOVERLAY FILE (System Compressed)\r\n\r\n");
        }

        for (const auto& attr : attrlist)
        {
            if (attr.TypeCode() == $REPARSE_POINT)
            {
                log::Info(_L_, L"\r\n$REPARSE_POINT\r\n\r\n");

                auto rp_attr = std::dynamic_pointer_cast<ReparsePointAttribute, MftRecordAttribute>(attr.Attribute());
                if (rp_attr == nullptr)
                    continue;

                if (SUCCEEDED(rp_attr->Parse()))
                {
                    log::Info(_L_, L"\tFlags=0x%X\r\n", rp_attr->Flags());
                    log::Info(_L_, L"\tPrint Name=%s\r\n", rp_attr->PrintName().c_str());
                    log::Info(_L_, L"\tSubstitute Name=%s\r\n", rp_attr->SubstituteName().c_str());
                }
            }
        };
    }

    log::Info(_L_, (L"\r\n"));
    return S_OK;
}

// Record command
HRESULT Main::PrintRecordDetails(ULONGLONG ullRecord)
{
    HRESULT hr = E_FAIL;
    LocationSet locs(_L_);

    std::vector<std::shared_ptr<Location>> addedLocs;
    if (FAILED(hr = locs.AddLocations(config.strVolume.c_str(), addedLocs)))
        return hr;

    std::shared_ptr<Location>& loc(addedLocs[0]);

    if (loc->GetReader() == nullptr)
    {
        log::Error(_L_, E_FAIL, L"Unable to instantiate Volume reader for location %s\r\n", loc->GetLocation().c_str());
        return E_FAIL;
    }

    if (FAILED(hr = loc->GetReader()->LoadDiskProperties()))
    {
        log::Error(_L_, hr, L"Unable to load disk properties for location %s\r\n", loc->GetLocation().c_str());
        return hr;
    }

    if (!loc->IsNTFS())
    {
        log::Error(
            _L_, hr, L"Record details command is only available on a NTFS volume (%s)\r\n", config.strVolume.c_str());
        return hr;
    }

    MFTWalker walk(_L_);

    if (FAILED(hr = walk.Initialize(addedLocs[0], true)))
    {
        log::Error(_L_, hr, L"Failed during %s MFT walk initialisation\r\n", config.strVolume.c_str());
    }

    MFTWalker::Callbacks callbacks;

    bool bRecordFound = false;

    callbacks.ElementCallback =
        [ullRecord, this, &hr, &bRecordFound](const std::shared_ptr<VolumeReader>& volReader, MFTRecord* pRecord) {
            if (ullRecord == pRecord->GetSafeMFTSegmentNumber() || ullRecord == pRecord->GetUnSafeMFTSegmentNumber())
            {
                PrintRecordDetails(volReader, pRecord);
                bRecordFound = true;
            }
        };

    callbacks.ProgressCallback = [&bRecordFound, this](ULONG dwProgress) -> HRESULT {
        if (bRecordFound)
        {
            log::Info(_L_, L"Record found, stopping enumeration\r\n");
            return HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES);
        }
        return S_OK;
    };

    if (FAILED(hr = walk.Walk(callbacks)))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
        {
            log::Verbose(_L_, L"Enumeration is stopping\r\n");
        }
        else
        {
            log::Error(_L_, hr, L"Failed during %s MFT walk\r\n");
        }
    }

    return S_OK;
}

// Find record
HRESULT Main::FindRecord(const std::wstring& strTerm)
{
    HRESULT hr = E_FAIL;

    return S_OK;
}

HRESULT Main::PrintMasterFileDetails()
{
    HRESULT hr = E_FAIL;
    LocationSet locs(_L_);

    std::vector<std::shared_ptr<Location>> addedLocs;
    if (FAILED(hr = locs.AddLocations(config.strVolume.c_str(), addedLocs)))
        return hr;

    locs.Consolidate(
        false,
        static_cast<FSVBR::FSType>(
            static_cast<uint32_t>(FSVBR::FSType::NTFS) | static_cast<uint32_t>(FSVBR::FSType::BITLOCKER)));

    for (const auto& loc : locs.GetAltitudeLocations())
    {
        auto volreader = loc->GetReader();

        if (volreader == nullptr)
        {
            log::Error(
                _L_, E_FAIL, L"Unable to instantiate Volume reader for location %s\r\n", loc->GetLocation().c_str());
            continue;
        }

        if (FAILED(hr = volreader->LoadDiskProperties()))
        {
            log::Error(_L_, hr, L"Unable to load disk properties for location %s\r\n", loc->GetLocation().c_str());
            continue;
        }

        if (!loc->IsNTFS())
        {
            log::Error(
                _L_, hr, L"MFT details command is only available for NTFS volumes\r\n", config.strVolume.c_str());
            continue;
        }

        if (Location::Type::OfflineMFT == loc->GetType())
        {
            std::unique_ptr<IMFT> pMFT;

            auto pOfflineReader = std::dynamic_pointer_cast<OfflineMFTReader>(loc->GetReader());
            if (pOfflineReader)
                pMFT = std::make_unique<MFTOffline>(_L_, pOfflineReader);
            else
                continue;

            if (FAILED(pMFT->Initialize()))
                continue;

            log::Info(
                _L_,
                L"\r\nMaster File Table for volume %s\r\n\tcontains %I64d records\r\n\tis located at ofset 0x%.16I64X "
                L"in the volume\r\n\r\n",
                loc->GetLocation().c_str(),
                pMFT->GetMFTRecordCount(),
                pMFT->GetMftOffset());
        }
        else
        {
            std::unique_ptr<MFTOnline> pMFT;

            pMFT = std::make_unique<MFTOnline>(_L_, loc->GetReader());
            if (pMFT == nullptr)
                return E_OUTOFMEMORY;

            if (FAILED(pMFT->Initialize()))
                return hr;

            log::Info(
                _L_,
                L"\r\nMaster File Table for volume %s\r\n\tcontains %d records\r\n\tis located at ofset 0x%.16I64X in "
                L"the volume\r\n\r\n",
                loc->GetLocation().c_str(),
                pMFT->GetMFTRecordCount(),
                pMFT->GetMftOffset());

            const auto& mftinfo = pMFT->GetMftInfo();

            log::Info(_L_, L"MFT has %d segments:\r\n", mftinfo.ExtentsVector.size());

            auto idx = 0L;

            for (const auto& extent : mftinfo.ExtentsVector)
            {
                if (extent.bZero)
                {
                    // Segment is SPARSE, only unallocated zeroes
                    log::Info(_L_, L"\t[%d] SPARSE, Size=0x%.16I64X\r\n", idx, extent.DataSize);
                }
                else
                {
                    log::Info(
                        _L_,
                        L"\t[%d] Offset=0x%.16I64X, Size=0x%.16I64X, Allocated=0x%.16I64X, LowestVCN=0x%.16I64X\r\n",
                        idx,
                        extent.DiskOffset,
                        extent.DataSize,
                        extent.DiskAlloc,
                        extent.LowestVCN);

                    ULONGLONG ullBytesRead = 0LL;
                    CBinaryBuffer buffer;

                    buffer.SetCount(1024);
                    if (FAILED(hr = volreader->Read(extent.DiskOffset, buffer, 1024, ullBytesRead)))
                    {
                        log::Error(_L_, hr, L"Failed to read first MFT record in extent\r\n");
                        break;
                    }

                    PFILE_RECORD_SEGMENT_HEADER pData = (PFILE_RECORD_SEGMENT_HEADER)buffer.GetData();

                    if ((pData->MultiSectorHeader.Signature[0] != 'F') || (pData->MultiSectorHeader.Signature[1] != 'I')
                        || (pData->MultiSectorHeader.Signature[2] != 'L')
                        || (pData->MultiSectorHeader.Signature[3] != 'E'))
                    {
                        log::Error(
                            _L_,
                            HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                            L"First MFT rec of extent %d doesn't have a file signature!\r\n",
                            idx);
                        break;
                    }
                    ULARGE_INTEGER li {0};

                    li.LowPart = pData->SegmentNumberLowPart;
                    li.HighPart = pData->SegmentNumberHighPart;

                    log::Info(
                        _L_,
                        L"\t\tFILE(0x%.16I64X) %s\r\n",
                        li.QuadPart,
                        ((li.QuadPart * 1024) / volreader->GetBytesPerCluster() == extent.LowestVCN
                             ? L"inSequence"
                             : L"outOfSequence"));
                }
                idx++;
            }

            log::Info(_L_, L"\r\n");

            // Print statistics about the MFT (after walking it)
            MFTWalker walker(_L_);

            if (FAILED(hr = walker.Initialize(loc)))
            {
                log::Warning(_L_, hr, L"Could not initialize walker for volume %s\r\n", loc->GetIdentifier().c_str());
                continue;
            }

            MFTWalker::Callbacks callbacks;
            ULONG ulCompleteRecords = 0L;

            callbacks.ElementCallback =
                [&ulCompleteRecords](const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt) {
                    ulCompleteRecords++;
                };

            if (FAILED(hr = walker.Walk(callbacks)))
            {
                log::Warning(_L_, hr, L"Could not wall volume %s\r\n", loc->GetIdentifier().c_str());
                continue;
            }

            auto successRate = (((double)ulCompleteRecords) / ((double)pMFT->GetMFTRecordCount())) * 100;

            log::Info(
                _L_,
                L"Successfully walked %d records (out of %d elements, %.2f%%)\r\n",
                ulCompleteRecords,
                pMFT->GetMFTRecordCount(),
                successRate);
        }
    }
    log::Info(_L_, L"\r\n");
    return S_OK;
}

// Quick way of checking if a character value is displayable ascii
static bool isAscii[0x100] =
    /*          0     1     2     3        4     5     6     7        8     9     A     B        C     D     E     F
     */
    /* 0x00 */ {false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0x10 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0x20 */ true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                /* 0x30 */ true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                /* 0x40 */ true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                /* 0x50 */ true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                /* 0x60 */ true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                /* 0x70 */ true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                false,
                /* 0x80 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0x90 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0xA0 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0xB0 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0xC0 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0xD0 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0xE0 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                /* 0xF0 */ false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false};

inline CHAR GetChar(BYTE aByte)
{
    return isAscii[aByte] ? (CHAR)aByte : '.';
}

// Print HexDump
HRESULT Main::PrintHexDump(DWORDLONG dwlOffset, DWORDLONG dwlSize)
{
    HRESULT hr = S_OK;

    LocationSet locs(_L_);

    std::vector<std::shared_ptr<Location>> addedLocs;
    if (FAILED(hr = locs.AddLocations(config.strVolume.c_str(), addedLocs)))
        return hr;

    auto volreader = addedLocs[0]->GetReader();
    if (volreader == nullptr)
    {
        log::Error(_L_, E_INVALIDARG, L"Failed to create a reader for volume %s\r\n", config.strVolume.c_str());
        return E_INVALIDARG;
    }

    if (FAILED(hr = volreader->LoadDiskProperties()))
        log::Error(_L_, hr, L"Failed to load disk properties for volume %s\r\n", config.strVolume.c_str());

    CBinaryBuffer buffer;
    buffer.SetCount(volreader->GetBytesPerCluster());

    ULONGLONG Read = 0LL;
    size_t BigOffset = 0;

    while (Read < dwlSize)
    {
        DWORDLONG ullThisRead = 0LL;
        ZeroMemory(buffer.GetData(), buffer.GetCount());
        if (FAILED(hr = volreader->Read(dwlOffset, buffer, volreader->GetBytesPerCluster(), ullThisRead)))
        {
            log::Error(_L_, hr, L"Failed to read volume %s\r\n", config.strVolume.c_str());
            return hr;
        }
        buffer.SetCount(std::min(
            (size_t)msl::utilities::SafeInt<size_t>(ullThisRead),
            static_cast<size_t>(
                dwlSize
                - Read)));  // If left to "print" is smaller than cluster size, then only print what is necessary
        buffer.PrintHex(_L_);

        Read += ullThisRead;
    }

    return S_OK;
}

// Print volume shadow copies
HRESULT Main::PrintVss()
{
    HRESULT hr = E_FAIL;

    VolumeShadowCopies vss(_L_);

    std::vector<VolumeShadowCopies::Shadow> shadows;

    if (SUCCEEDED(hr = vss.EnumerateShadows(shadows)))
    {
        std::shared_ptr<TableOutput::IWriter> pVssWriter;

        if (config.output.Type != OutputSpec::Kind::None)
        {
            if (nullptr == (pVssWriter = TableOutput::GetWriter(_L_, config.output)))
            {
                log::Error(_L_, E_FAIL, L"Failed to create output file.\r\n");
                return E_FAIL;
            }
        }
        BOOST_SCOPE_EXIT(&pVssWriter)
        {
            if (nullptr != pVssWriter)
                pVssWriter->Close();
        }
        BOOST_SCOPE_EXIT_END;

        if (pVssWriter != nullptr)
        {
            auto& output = pVssWriter->GetTableOutput();

            for (const auto& shadow : shadows)
            {
                output.WriteGUID(shadow.guid);
                output.WriteString(shadow.DeviceInstance.c_str());
                output.WriteString(shadow.VolumeName.c_str());
                output.WriteFileTime(shadow.CreationTime);
                output.WriteFlags(shadow.Attributes, VolumeShadowCopies::g_VssFlags, L'|');
                output.WriteEndOfLine();
            }
        }
        else
        {
            for (const auto& shadow : shadows)
            {
                WCHAR szCLSID[MAX_GUID_STRLEN];
                if (!StringFromGUID2(shadow.guid, szCLSID, MAX_GUID_STRLEN))
                {
                    szCLSID[0] = L'\0';
                }

                log::Info(_L_, L"\r\nVSS Snapshot :\r\n");
                log::Info(_L_, L"-> guid             : %s\r\n", szCLSID);
                log::Info(_L_, L"-> device instance  : %s\r\n", shadow.DeviceInstance.c_str());
                log::Info(_L_, L"-> volume name      : %s\r\n", shadow.VolumeName.c_str());
                log::Info(_L_, L"-> creation time    : ");
                _L_->WriteFileTime(shadow.CreationTime);
                log::Info(_L_, L"\r\n-> attributes       : ");
                _L_->WriteFlags(shadow.Attributes, VolumeShadowCopies::g_VssFlags, L'|');
                log::Info(_L_, L"\r\n");
            }
        }
    }
    else
    {
        log::Error(_L_, hr, L"Failed to list volume shadow copies.\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    switch (config.cmd)
    {
        case Main::DetailLocation:
            return PrintLocationDetails();
        case Main::EnumLocations:
            return PrintAllLocations();
        case Main::MFT:
            return PrintMasterFileDetails();
        case Main::USN:
        {
            HANDLE hVolume = OpenVolume(GetVolumePath().c_str());
            if (hVolume == INVALID_HANDLE_VALUE)
                return E_FAIL;

            BOOST_SCOPE_EXIT(&hVolume)
            {
                if (hVolume != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(hVolume);
                    hVolume = INVALID_HANDLE_VALUE;
                }
            }
            BOOST_SCOPE_EXIT_END;

            if (FAILED(hr = PrintUSNJournalConfiguration(hVolume)))
            {
                log::Error(_L_, hr, L"Failed to obtain USN configuration values for volume\r\n", GetVolumePath());
                return hr;
            }
            if (ShouldConfigureUSNJournal())
            {
                if (FAILED(hr = ConfigureUSNJournal(hVolume)))
                {
                    log::Info(
                        _L_,
                        L"Configure USN   : Failed to configure USN journal for volume %s\r\n",
                        GetVolumePath().c_str());
                }
                else
                {
                    if (hr == S_OK)
                    {
                        log::Info(
                            _L_,
                            L"Configure USN   : Successfully configured USN journal for volume %s\r\n",
                            GetVolumePath().c_str());
                    }
                    else if (hr == S_FALSE)
                    {
                        log::Info(
                            _L_,
                            L"Configure USN   : USN journal for volume %s is bigger than minsize \r\n",
                            GetVolumePath().c_str());
                    }
                    if (FAILED(hr = PrintUSNJournalConfiguration(hVolume)))
                    {
                        log::Error(
                            _L_,
                            hr,
                            L"Failed to obtain new USN configuration values for volume\r\n",
                            GetVolumePath().c_str());
                    }
                }
            }
            return S_OK;
        }
        case Main::Find:
        {
            return S_OK;
        }
        case Main::Record:
        {
            return PrintRecordDetails(config.ullRecord);
        }
        case Main::HexDump:
        {
            return PrintHexDump(config.dwlOffset, config.dwlSize);
        }
        case Main::Vss:
        {
            return PrintVss();
        }
    }
    return S_OK;
}
