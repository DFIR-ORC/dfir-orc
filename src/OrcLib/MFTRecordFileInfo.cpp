//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <sstream>

#include "MFTRecordFileInfo.h"

#include "MFTWalker.h"
#include "MftRecordAttribute.h"
#include "WideAnsi.h"
#include "NtDllExtension.h"

#include "MountedVolumeReader.h"
#include "SnapshotVolumeReader.h"

#include "Buffer.h"

#include <fmt/format.h>

using namespace std;

using namespace Orc;

typedef NTSTATUS(__stdcall* pvfNtOpenFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
typedef NTSTATUS(__stdcall* pvfNtQueryInformationFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

MFTRecordFileInfo::MFTRecordFileInfo(
    std::wstring strComputerName,
    const std::shared_ptr<VolumeReader>& pVolReader,
    Intentions dwDefaultIntentions,
    const std::vector<Filter>& filters,
    LPCWSTR szFullFileName,
    MFTRecord* pRecord,
    const PFILE_NAME pFileName,
    const std::shared_ptr<DataAttribute>& pDataAttr,
    Authenticode& verifytrust)
    : NtfsFileInfo(
        std::move(strComputerName),
        pVolReader,
        dwDefaultIntentions,
        filters,
        szFullFileName,
        (DWORD)wcslen(szFullFileName),
        verifytrust)
{
    m_pMFTRecord = pRecord;
    m_pFileName = pFileName;
    m_pDataAttr = pDataAttr;
}

HRESULT MFTRecordFileInfo::Open()
{
    if (!m_szFullName)
        return E_POINTER;
    if (m_pMFTRecord == NULL)
        return E_POINTER;

    DWORD dwRequiredAccess = GetRequiredAccessMask(NtfsFileInfo::g_NtfsColumnNames);

    if (dwRequiredAccess)
    {
        OBJECT_ATTRIBUTES ObjAttr;
        UNICODE_STRING sFileID = {8, 8, (WCHAR*)&(m_pMFTRecord->m_FileReferenceNumber)};
#define FILE_OPEN_BY_FILE_ID 0x00002000
#define FILE_NON_DIRECTORY_FILE 0x00000040
#define FILE_RANDOM_ACCESS 0x00000800
#define FILE_SEQUENTIAL_ONLY 0x00000004
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#define FILE_SYNCHRONOUS_IO_ALERT 0x00000010
#define FILE_OPEN_FOR_BACKUP_INTENT 0x00004000
#define OBJ_CASE_INSENSITIVE 0x00000040L

#ifndef InitializeObjectAttributes
#    define InitializeObjectAttributes(p, n, a, r, s)                                                                  \
        {                                                                                                              \
            (p)->Length = sizeof(OBJECT_ATTRIBUTES);                                                                   \
            (p)->RootDirectory = r;                                                                                    \
            (p)->Attributes = a;                                                                                       \
            (p)->ObjectName = n;                                                                                       \
            (p)->SecurityDescriptor = s;                                                                               \
            (p)->SecurityQualityOfService = NULL;                                                                      \
        }
#endif
        const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();
        if (pNtDll == nullptr)
        {
            return E_FAIL;
        }

        IO_STATUS_BLOCK IoStatusBlock;
        InitializeObjectAttributes(&ObjAttr, &sFileID, OBJ_CASE_INSENSITIVE, m_pVolReader->GetDevice(), NULL);

        HRESULT hr = pNtDll->NtOpenFile(
            &m_hFile,
            dwRequiredAccess | SYNCHRONIZE,
            &ObjAttr,
            &IoStatusBlock,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OPEN_BY_FILE_ID | FILE_SEQUENTIAL_ONLY | FILE_SYNCHRONOUS_IO_ALERT | FILE_OPEN_FOR_BACKUP_INTENT);

        if (IoStatusBlock.Status != STATUS_SUCCESS)
        {
            // Short term bug fix in case of STATUS_SHARING_VIOLATION.
            // ideally we should begin with less rigths and progressively increase the need rigths....
            // However, the impact on performance would be too high.
#define STATUS_SHARING_VIOLATION ((NTSTATUS)0xC0000043L)
            if (HRESULT_FROM_NT(STATUS_SHARING_VIOLATION) == hr)
            {
                DWORD dwReducedAccess = dwRequiredAccess & ~FILE_READ_DATA;
                hr = pNtDll->NtOpenFile(
                    &m_hFile,
                    dwReducedAccess | SYNCHRONIZE,
                    &ObjAttr,
                    &IoStatusBlock,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    FILE_OPEN_BY_FILE_ID | FILE_SEQUENTIAL_ONLY | FILE_SYNCHRONOUS_IO_ALERT
                        | FILE_OPEN_FOR_BACKUP_INTENT);
            }
            return hr;
        }
#undef FILE_OPEN_BY_FILE_ID
#undef OBJ_CASE_INSENSITIVE
#undef OBJ_KERNEL_HANDLE
#undef STATUS_SUCCESS
#undef FILE_SEQUENTIAL_ONLY
#undef InitializeObjectAttributes
    }
    return S_OK;
}

bool MFTRecordFileInfo::IsDirectory()
{
    return m_pMFTRecord->IsDirectory();
}

std::shared_ptr<ByteStream> MFTRecordFileInfo::GetFileStream()
{
    if (m_pDataAttr == nullptr)
        return nullptr;

    auto retval = m_pDataAttr->GetDataStream(m_pVolReader);

    m_pDataAttr->GetDetails()->SetDataStream(retval);

    return retval;
}

bool MFTRecordFileInfo::ExceedsFileThreshold(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
    if (m_pMFTRecord == NULL)
        return false;
    if (m_pMFTRecord->m_bIsDirectory)
        return false;

    if (m_pDataAttr != NULL)
    {
        // We have the default Data stream
        if (m_pDataAttr->Header()->FormCode == RESIDENT_FORM)
        {
            if (nFileSizeHigh > 0)
                return true;
            if (m_pDataAttr->Header()->Form.Nonresident.FileSize > nFileSizeLow)
                return true;
        }
        else
        {
            LARGE_INTEGER FileSize;
            FileSize.QuadPart = m_pDataAttr->Header()->Form.Nonresident.FileSize;
            if (((DWORD)FileSize.HighPart) > nFileSizeHigh)
                return true;
            if (((DWORD)FileSize.LowPart) > nFileSizeLow)
                return true;
        }
    }
    return false;
}

HRESULT MFTRecordFileInfo::WriteFileName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pFileName == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    auto fileName = std::wstring_view(m_pFileName->FileName, m_pFileName->FileNameLength);

    if (m_pDataAttr != NULL)
    {
        PATTRIBUTE_RECORD_HEADER pHeader = m_pDataAttr->Header();
        if (pHeader->NameLength)
        {
            auto streamName = std::wstring_view((WCHAR*)(((BYTE*)pHeader) + pHeader->NameOffset), pHeader->NameLength);

            hr = output.WriteFormated(L"{}:{}", fileName, streamName);

            return hr;
        }
    }

    hr = output.WriteString(fileName);
    return hr;
}

HRESULT MFTRecordFileInfo::WriteShortName(ITableOutput& output)
{
    for (auto iter = m_pMFTRecord->m_FileNames.begin(); iter != m_pMFTRecord->m_FileNames.end(); ++iter)
    {
        PFILE_NAME pFileName = *iter;

        if (pFileName->Flags & FILE_NAME_DOS83
            && (NtfsSegmentNumber(&pFileName->ParentDirectory) == (NtfsSegmentNumber(&m_pFileName->ParentDirectory)))
            && wcsncmp(
                m_pFileName->FileName,
                pFileName->FileName,
                min(m_pFileName->FileNameLength, pFileName->FileNameLength)))
        {
            // Filename is DOS83&same parent&& not same as full file name
            auto shortName = std::wstring_view(pFileName->FileName, pFileName->FileNameLength);

            return output.WriteString(shortName);
        }
    }
    return output.WriteNothing();
}

HRESULT MFTRecordFileInfo::WriteAttributes(ITableOutput& output)
{
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    ULONG ulAttrs = m_pMFTRecord->m_pStandardInformation->FileAttributes;
    if (m_pMFTRecord->IsDirectory())
        ulAttrs |= FILE_ATTRIBUTE_DIRECTORY;
    else
        ulAttrs |= FILE_ATTRIBUTE_NORMAL;
    return output.WriteAttributes(ulAttrs);
}

HRESULT MFTRecordFileInfo::WriteSecDescrID(ITableOutput& output)
{
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    return output.WriteInteger(m_pMFTRecord->m_pStandardInformation->SecurityId);
}

HRESULT MFTRecordFileInfo::WriteSizeInBytes(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    if (m_pDataAttr == NULL)
        return output.WriteFileSize(0L, 0L);

    // We have the default Data stream
    if (m_pDataAttr->Header()->FormCode == RESIDENT_FORM)
    {
        if (FAILED(hr = output.WriteFileSize(0L, m_pDataAttr->Header()->Form.Resident.ValueLength)))
        {
            return hr;
        }
    }
    else
    {
        LARGE_INTEGER FileSize;
        FileSize.QuadPart = m_pDataAttr->Header()->Form.Nonresident.FileSize;
        if (FAILED(hr = output.WriteFileSize(FileSize)))
        {
            return hr;
        }
    }

    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteCreationDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    if (FAILED(hr = output.WriteFileTime(m_pMFTRecord->m_pStandardInformation->CreationTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteLastModificationDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (FAILED(hr = output.WriteFileTime(m_pMFTRecord->m_pStandardInformation->LastModificationTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteLastAttrChangeDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (FAILED(hr = output.WriteFileTime(m_pMFTRecord->m_pStandardInformation->LastChangeTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteLastAccessDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (FAILED(hr = output.WriteFileTime(m_pMFTRecord->m_pStandardInformation->LastAccessTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteUSN(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    if (FAILED(hr = output.WriteInteger((DWORDLONG)m_pMFTRecord->m_pStandardInformation->USN)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteFRN(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    LARGE_INTEGER* pLI = (LARGE_INTEGER*)&m_pMFTRecord->m_FileReferenceNumber;
    if (FAILED(hr = output.WriteInteger((DWORDLONG)pLI->QuadPart)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteParentFRN(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    if (m_pFileName != nullptr)
    {
        LARGE_INTEGER* pLI = (LARGE_INTEGER*)&m_pFileName->ParentDirectory;
        if (FAILED(hr = output.WriteInteger((DWORDLONG)pLI->QuadPart)))
        {
            return hr;
        }
    }
    else
    {
        if (FAILED(hr = output.WriteInteger((DWORDLONG)0)))
        {
            return hr;
        }
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteADS(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    const vector<shared_ptr<DataAttribute>>& dataattrs = m_pMFTRecord->GetDataAttributes();

    if (dataattrs.size())
    {
        bool bIsFirst = true;
        Buffer<WCHAR, ORC_MAX_PATH> ADSs;

        for (auto iter = dataattrs.begin(); iter != dataattrs.end(); ++iter)
        {
            const shared_ptr<DataAttribute>& Attr = *iter;

            PATTRIBUTE_RECORD_HEADER pAttr = Attr->Header();

            if (pAttr->NameLength)
            {
                auto adsName = std::wstring_view((WCHAR*)((BYTE*)pAttr + pAttr->NameOffset), pAttr->NameLength);

                if (!bIsFirst)
                {
                    fmt::format_to(std::back_inserter(ADSs), L";{}", adsName);
                }
                else
                {
                    fmt::format_to(std::back_inserter(ADSs), L"{}", adsName);
                    bIsFirst = false;
                }
            }
        }
        if (ADSs.empty())
            return output.WriteNothing();
        else
        {
            if (FAILED(hr = output.WriteString(std::wstring_view(ADSs.get(), ADSs.size()))))
                return hr;
        }
    }
    else
        return output.WriteNothing();

    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteFilenameID(ITableOutput& output)
{
    const auto& attrs = m_pMFTRecord->GetAttributeList();

    USHORT usInstanceID = 0L;
    std::for_each(begin(attrs), end(attrs), [this, &usInstanceID](const AttributeListEntry& attr) {
        if (attr.TypeCode() == $FILE_NAME)
        {
            const auto& attribute = attr.Attribute();
            if (attribute)
            {
                auto header = (PATTRIBUTE_RECORD_HEADER)attribute->Header();
                if (m_pFileName == (PFILE_NAME)((LPBYTE)header + header->Form.Resident.ValueOffset))
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
    if (usInstanceID > 0)
        return output.WriteInteger((DWORD)usInstanceID);
    else
        return output.WriteNothing();
}

HRESULT MFTRecordFileInfo::WriteFilenameFlags(ITableOutput& output)
{
    if (m_pFileName != NULL)
        return output.WriteInteger((DWORD)m_pFileName->Flags);
    else
        return output.WriteNothing();
}

HRESULT MFTRecordFileInfo::WriteDataID(ITableOutput& output)
{
    if (m_pDataAttr != nullptr)
        return output.WriteInteger((DWORD)m_pDataAttr->Header()->Instance);
    else
        return output.WriteNothing();
}

HRESULT MFTRecordFileInfo::WriteFilenameIndex(ITableOutput& output)
{
    if (m_pFileName != NULL)
        return output.WriteInteger((DWORD)m_pMFTRecord->GetFileNameIndex(m_pFileName));
    else
        return output.WriteNothing();
}

HRESULT MFTRecordFileInfo::WriteDataIndex(ITableOutput& output)
{
    if (m_pDataAttr != NULL)
        return output.WriteInteger((DWORD)m_pMFTRecord->GetDataIndex(m_pDataAttr));
    else
        return output.WriteNothing();
}

HRESULT MFTRecordFileInfo::WriteSnapshotID(ITableOutput& output)
{
    auto snapshot_reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(m_pVolReader);

    if (snapshot_reader)
        return output.WriteGUID(snapshot_reader->GetSnapshotID());
    else
        return output.WriteGUID(GUID_NULL);
}

HRESULT MFTRecordFileInfo::WriteRecordInUse(ITableOutput& output)
{
    return output.WriteBool(m_pMFTRecord->m_pRecord->Flags & FILE_RECORD_SEGMENT_IN_USE);
}

HRESULT MFTRecordFileInfo::WriteFileNameLastAttrModificationDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pFileName == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (FAILED(hr = output.WriteFileTime(m_pFileName->Info.LastChangeTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteFileNameLastModificationDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pFileName == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (FAILED(hr = output.WriteFileTime(m_pFileName->Info.LastModificationTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteFileNameCreationDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pFileName == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (FAILED(hr = output.WriteFileTime(m_pFileName->Info.CreationTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteFileNameLastAccessDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (m_pFileName == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (FAILED(hr = output.WriteFileTime(m_pFileName->Info.LastAccessTime)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteOwnerId(ITableOutput& output)
{
    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }
    if (m_pMFTRecord->m_pStandardInformation == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    return output.WriteInteger(m_pMFTRecord->m_pStandardInformation->OwnerId);
}

HRESULT MFTRecordFileInfo::WriteExtendedAttributes(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    if (!m_pMFTRecord->HasExtendedAttr())
    {
        output.WriteNothing();
        return S_OK;
    }

    const auto& attrlist = m_pMFTRecord->GetAttributeList();

    wstringstream EANameStream;

    std::for_each(attrlist.begin(), attrlist.end(), [&EANameStream, this](const AttributeListEntry& item) {
        if (item.TypeCode() == $EA)
        {
            auto pAttr = std::dynamic_pointer_cast<ExtendedAttribute, MftRecordAttribute>(item.Attribute());

            if (pAttr)
            {
                if (FAILED(pAttr->Parse(this->m_pVolReader)))
                    return;

                const auto& items = pAttr->Items();

                wstringstream& transstream = EANameStream;

                std::for_each(items.begin(), items.end(), [&transstream](const pair<wstring, CBinaryBuffer>& eaitem) {
                    transstream << eaitem.first << L";";
                });
            }
            else
            {
                EANameStream << "<MissingEA>" << L";";
            }
            return;
        }
    });

    if (FAILED(hr = output.WriteString(EANameStream.str().c_str())))
    {
        return hr;
    }
    return S_OK;
}

HRESULT MFTRecordFileInfo::WriteEASize(ITableOutput& output)
{

    if (m_pMFTRecord == NULL)
    {
        output.AbandonColumn();
        return E_POINTER;
    }

    if (!m_pMFTRecord->HasExtendedAttr())
    {
        output.WriteNothing();
        return S_OK;
    }

    const auto& attrlist = m_pMFTRecord->GetAttributeList();
    DWORDLONG dwlDataSize = 0LL;

    for (const auto& item : attrlist)
    {
        if (item.TypeCode() != $EA)
        {
            continue;
        }

        auto attribute = item.Attribute();
        if (attribute == nullptr)
        {
            break;
        }

        HRESULT hr = attribute->DataSize(m_pVolReader, dwlDataSize);
        if (FAILED(hr))
        {
            dwlDataSize = 0L;
        }

        break;
    }

    return output.WriteInteger(dwlDataSize);
}

MFTRecordFileInfo::~MFTRecordFileInfo(void) {}
