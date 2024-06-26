//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "USNRecordFileInfo.h"
#include "VolumeReader.h"
#include "TableOutput.h"

#include "NtDllExtension.h"

#include "FileStream.h"

#include "Log/Log.h"

using namespace Orc;

typedef NTSTATUS(__stdcall* pvfNtOpenFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
typedef NTSTATUS(__stdcall* pvfNtQueryInformationFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

static const auto FILEINFO_STREAMINFO_BUFLEN = (1024);
static const auto FILEINFO_STREAMNAME_BUFLEN = (1024);

USNRecordFileInfo::USNRecordFileInfo(
    LPCWSTR szComputerName,
    const std::shared_ptr<VolumeReader>& pVolReader,
    Intentions dwDefaultIntentions,
    const std::vector<Filter>& filters,
    WCHAR* szFullFileName,
    USN_RECORD* pElt,
    Authenticode& verifytrust)
    : NtfsFileInfo(
        szComputerName,
        pVolReader,
        dwDefaultIntentions,
        filters,
        szFullFileName,
        (DWORD)wcslen(szFullFileName),
        verifytrust)
{
    m_pUSNRecord = pElt;

    m_hFile = INVALID_HANDLE_VALUE;

    m_bFileInformationAvailable = false;

    m_bFileHasStreams = false;
    m_bStreamInformationAvailable = false;
    m_pFileStreamInformation = NULL;

    m_bFileFindAvailable = false;

    m_dwOpenStatus = ERROR_SUCCESS;
    m_bIsOpenStatusValid = false;
}

HRESULT USNRecordFileInfo::Open()
{
    if (!m_szFullName)
        return E_POINTER;
    if (m_pUSNRecord == NULL)
        return E_POINTER;

    DWORD dwRequiredAccess = GetRequiredAccessMask(NtfsFileInfo::g_NtfsColumnNames);

    if (dwRequiredAccess)
    {
        OBJECT_ATTRIBUTES ObjAttr;
        UNICODE_STRING sFileID = {8, 8, (WCHAR*)&(m_pUSNRecord->FileReferenceNumber)};
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

        InitializeObjectAttributes(&ObjAttr, &sFileID, OBJ_CASE_INSENSITIVE, m_pVolReader->GetDevice(), NULL);

        IO_STATUS_BLOCK IoStatusBlock;

        const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();
        if (pNtDll == nullptr)
        {
            return E_FAIL;
        }

        HRESULT hr = pNtDll->NtOpenFile(
            &m_hFile,
            dwRequiredAccess | SYNCHRONIZE,
            &ObjAttr,
            &IoStatusBlock,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OPEN_BY_FILE_ID | FILE_SEQUENTIAL_ONLY | FILE_SYNCHRONOUS_IO_ALERT | FILE_OPEN_FOR_BACKUP_INTENT);

        if (IoStatusBlock.Status == STATUS_SUCCESS)
        {
            m_bIsOpenStatusValid = true;
            m_dwOpenStatus = ERROR_SUCCESS;
        }
        else
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
            m_bIsOpenStatusValid = true;
            return m_dwOpenStatus = hr;
        }
#undef FILE_OPEN_BY_FILE_ID
#undef OBJ_CASE_INSENSITIVE
#undef OBJ_KERNEL_HANDLE
#undef FILE_SEQUENTIAL_ONLY
#undef InitializeObjectAttributes
    }
    m_bIsOpenStatusValid = false;
    return S_OK;
}

bool USNRecordFileInfo::IsDirectory()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckFileInformation()))
        return false;

    if (m_fiInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        return true;
    }
    return false;
}

std::shared_ptr<ByteStream> USNRecordFileInfo::GetFileStream()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckFileHandle()))
        return nullptr;

    auto retval = std::make_shared<FileStream>();

    if (FAILED(hr = retval->CopyHandle(m_hFile)))
    {
        Log::Debug(L"Failed to open file '{}' [{}]", m_szFullName, SystemError(hr));
        return nullptr;
    }

    GetDetails()->SetDataStream(retval);
    return retval;
}

HRESULT USNRecordFileInfo::OpenFileInformation()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckFileHandle()))
        return hr;
    if (!m_bFileInformationAvailable)
        if (!GetFileInformationByHandle(m_hFile, &m_fiInfo))
            return HRESULT_FROM_WIN32(GetLastError());
    m_bFileInformationAvailable = true;
    return S_OK;
}

bool USNRecordFileInfo::ExceedsFileThreshold(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckFileInformation()))
        return false;
    if (m_fiInfo.nFileSizeHigh > nFileSizeHigh)
        return true;
    if (m_fiInfo.nFileSizeLow > nFileSizeLow)
        return true;
    return false;
}

typedef enum _NT_FILE_INFORMATION_CLASS
{
    NT_FileDirectoryInformation = 1,
    FileFullDirectoryInformation,  // 2
    FileBothDirectoryInformation,  // 3
    FileBasicInformation,  // 4
    FileStandardInformation,  // 5
    FileInternalInformation,  // 6
    FileEaInformation,  // 7
    FileAccessInformation,  // 8
    FileNameInformation,  // 9
    FileRenameInformation,  // 10
    FileLinkInformation,  // 11
    FileNamesInformation,  // 12
    FileDispositionInformation,  // 13
    FilePositionInformation,  // 14
    FileFullEaInformation,  // 15
    FileModeInformation,  // 16
    FileAlignmentInformation,  // 17
    FileAllInformation,  // 18
    FileAllocationInformation,  // 19
    FileEndOfFileInformation,  // 20
    FileAlternateNameInformation,  // 21
    FileStreamInformation,  // 22
    FilePipeInformation,  // 23
    FilePipeLocalInformation,  // 24
    FilePipeRemoteInformation,  // 25
    FileMailslotQueryInformation,  // 26
    FileMailslotSetInformation,  // 27
    FileCompressionInformation,  // 28
    FileObjectIdInformation,  // 29
    FileCompletionInformation,  // 30
    FileMoveClusterInformation,  // 31
    FileQuotaInformation,  // 32
    FileReparsePointInformation,  // 33
    FileNetworkOpenInformation,  // 34
    FileAttributeTagInformation,  // 35
    FileTrackingInformation,  // 36
    FileIdBothDirectoryInformation,  // 37
    FileIdFullDirectoryInformation,  // 38
    FileValidDataLengthInformation,  // 39
    FileShortNameInformation,  // 40
    FileIoCompletionNotificationInformation,  // 41
    FileIoStatusBlockRangeInformation,  // 42
    FileIoPriorityHintInformation,  // 43
    FileSfioReserveInformation,  // 44
    FileSfioVolumeInformation,  // 45
    FileHardLinkInformation,  // 46
    FileProcessIdsUsingFileInformation,  // 47
    FileNormalizedNameInformation,  // 48
    FileNetworkPhysicalNameInformation,  // 49
    FileIdGlobalTxDirectoryInformation,  // 50
    FileMaximumInformation
} NT_FILE_INFORMATION_CLASS,
    *PNT_FILE_INFORMATION_CLASS;

HRESULT USNRecordFileInfo::OpenStreamInformation()
{
    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();

    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckFileHandle()))
        return hr;

    DWORD dwMultiplier = 1L;

    do
    {
        m_pFileStreamInformation =
            (FILE_STREAM_INFORMATION*)HeapAlloc(GetProcessHeap(), 0L, (FILEINFO_STREAMINFO_BUFLEN * dwMultiplier));
        if (!m_pFileStreamInformation)
            return E_OUTOFMEMORY;

        ZeroMemory(m_pFileStreamInformation, FILEINFO_STREAMINFO_BUFLEN * dwMultiplier);

        IO_STATUS_BLOCK StatusBlock;
        HRESULT hr = pNtDll->NtQueryInformationFile(
            m_hFile,
            &StatusBlock,
            m_pFileStreamInformation,
            FILEINFO_STREAMINFO_BUFLEN,
            (FILE_INFORMATION_CLASS)FileStreamInformation);

        switch (hr)
        {
            case HRESULT_FROM_NT( 0L ):
                m_bStreamInformationAvailable = true;
                break;
            case HRESULT_FROM_NT(STATUS_BUFFER_OVERFLOW):
            case HRESULT_FROM_NT(STATUS_BUFFER_TOO_SMALL):
                HeapFree(GetProcessHeap(), 0L, m_pFileStreamInformation);
                m_pFileStreamInformation = NULL;
                dwMultiplier++;
                break;
            default:
                return hr;
        }
    } while (m_pFileStreamInformation == NULL);

    return S_OK;
}
HRESULT USNRecordFileInfo::OpenFileFind()
{
    HANDLE hFind = INVALID_HANDLE_VALUE;

    if (!m_bFileFindAvailable)
        if ((hFind = FindFirstFile(m_szFullName, &m_FileFindData)) == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());
    m_bFileFindAvailable = true;
    if (hFind != INVALID_HANDLE_VALUE && hFind != NULL)
        FindClose(hFind);
    return S_OK;
}

HRESULT USNRecordFileInfo::WriteVolumeID(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    hr = output.WriteString(L"VolID");
    return hr;
}

HRESULT USNRecordFileInfo::WriteFileName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (m_pUSNRecord == NULL)
        return E_POINTER;
    WCHAR savedchar = *((WCHAR*)((BYTE*)m_pUSNRecord->FileName + m_pUSNRecord->FileNameLength));
    *((WCHAR*)((BYTE*)m_pUSNRecord->FileName + m_pUSNRecord->FileNameLength)) = L'\0';

    hr = output.WriteString(m_pUSNRecord->FileName);

    *((WCHAR*)((BYTE*)m_pUSNRecord->FileName + m_pUSNRecord->FileNameLength)) = savedchar;

    return hr;
}

HRESULT USNRecordFileInfo::WriteShortName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckFileFind()))
        return hr;
    return output.WriteString(m_FileFindData.cAlternateFileName);
}

HRESULT USNRecordFileInfo::WriteParentName(ITableOutput& output)
{
    if (m_pUSNRecord == NULL)
        return E_POINTER;

    const_cast<LPWSTR>(m_szFullName)[m_dwFullNameLen - (m_pUSNRecord->FileNameLength / sizeof(WCHAR)) - 1] = 0;
    HRESULT hr = output.WriteString(m_szFullName);
    const_cast<LPWSTR>(m_szFullName)[m_dwFullNameLen - (m_pUSNRecord->FileNameLength / sizeof(WCHAR)) - 1] = L'\\';
    return hr;
}

HRESULT USNRecordFileInfo::WriteAttributes(ITableOutput& output)
{
    if (m_pUSNRecord == NULL)
        return E_POINTER;

    return output.WriteAttributes(m_pUSNRecord->FileAttributes);
}

HRESULT USNRecordFileInfo::WriteSecDescrID(ITableOutput& output)
{
    if (m_pUSNRecord == NULL)
        return E_POINTER;

    return output.WriteInteger(m_pUSNRecord->SecurityId);
}

HRESULT USNRecordFileInfo::WriteSizeInBytes(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckFileInformation()))
        return hr;
    return output.WriteFileSize(m_fiInfo.nFileSizeHigh, m_fiInfo.nFileSizeLow);
}

HRESULT USNRecordFileInfo::WriteCreationDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckFileInformation()))
        return hr;
    return output.WriteFileTime(m_fiInfo.ftCreationTime);
}

HRESULT USNRecordFileInfo::WriteLastModificationDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckFileInformation()))
        return hr;
    return output.WriteFileTime(m_fiInfo.ftLastWriteTime);
}

HRESULT USNRecordFileInfo::WriteRecordInUse(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);

    return E_NOTIMPL;
}

HRESULT USNRecordFileInfo::WriteLastAccessDate(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckFileInformation()))
        return hr;
    return output.WriteFileTime(m_fiInfo.ftLastAccessTime);
}

HRESULT USNRecordFileInfo::WriteADS(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CheckStreamInformation()))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY))
            return output.WriteString(L"");
        return hr;
    }

    if (m_bStreamInformationAvailable && m_pFileStreamInformation)
    {
        FILE_STREAM_INFORMATION* pCur = m_pFileStreamInformation;
        DWORD dwStreamNamesLen = 0L;
        do
        {
            if (wcsncmp(L"::$DATA", pCur->StreamName, pCur->StreamNameLength))
                dwStreamNamesLen += (pCur->StreamNameLength + 1);  // trailing \0 or separator
            if (pCur->NextEntryOffset)
                pCur = (FILE_STREAM_INFORMATION*)((BYTE*)pCur + pCur->NextEntryOffset);
            else
                break;
        } while (1);

        if (dwStreamNamesLen > 0)
        {
            WCHAR* szBuf = (WCHAR*)malloc(msl::utilities::SafeInt<USHORT>(dwStreamNamesLen) * sizeof(WCHAR));
            if (szBuf == NULL)
                return E_OUTOFMEMORY;
            ZeroMemory(szBuf, dwStreamNamesLen * sizeof(WCHAR));

            pCur = m_pFileStreamInformation;

            do
            {
                if (wcsncmp(L"::$DATA", pCur->StreamName, pCur->StreamNameLength))
                {
                    wcsncat_s(
                        szBuf,
                        dwStreamNamesLen,
                        pCur->StreamName,
                        (pCur->StreamNameLength - 12 /* -6 to remove the trailing :$DATA*/) / sizeof(WCHAR));
                    if (pCur->NextEntryOffset)
                        wcsncat_s(szBuf, dwStreamNamesLen, L";", 1);
                }
                if (pCur->NextEntryOffset)
                    pCur = (FILE_STREAM_INFORMATION*)((BYTE*)pCur + pCur->NextEntryOffset);
                else
                    break;
            } while (1);

            output.WriteString(szBuf);
            free(szBuf);
        }
        else
            output.WriteString(L"");
    }
    return S_OK;
}

HRESULT USNRecordFileInfo::WriteFilenameFlags(ITableOutput& output)
{
    return output.WriteNothing();
}

HRESULT USNRecordFileInfo::WriteFilenameID(ITableOutput& output)
{
    return output.WriteNothing();
}

HRESULT USNRecordFileInfo::WriteDataID(ITableOutput& output)
{
    return output.WriteNothing();
}

HRESULT USNRecordFileInfo::WriteUSN(ITableOutput& output)
{
    if (m_pUSNRecord == NULL)
        return E_POINTER;

    return output.WriteInteger((DWORDLONG)m_pUSNRecord->Usn);
}

HRESULT USNRecordFileInfo::WriteFRN(ITableOutput& output)
{
    if (m_pUSNRecord == NULL)
        return E_POINTER;

    return output.WriteInteger(m_pUSNRecord->FileReferenceNumber);
}

HRESULT USNRecordFileInfo::WriteParentFRN(ITableOutput& output)
{
    if (m_pUSNRecord == NULL)
        return E_POINTER;

    return output.WriteInteger(m_pUSNRecord->ParentFileReferenceNumber);
}

USNRecordFileInfo::~USNRecordFileInfo(void)
{
    if (m_pFileStreamInformation)
        HeapFree(GetProcessHeap(), 0L, m_pFileStreamInformation);
}
