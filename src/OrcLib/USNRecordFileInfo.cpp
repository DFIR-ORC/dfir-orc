//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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
#include "Utils/BufferView.h"

#include <cstddef>
#include <new>
#include <string>
#include <string_view>

using namespace Orc;

typedef NTSTATUS(__stdcall* pvfNtOpenFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
typedef NTSTATUS(__stdcall* pvfNtQueryInformationFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

static const auto FILEINFO_STREAMINFO_BUFLEN = (1024);
static const auto FILEINFO_STREAMNAME_BUFLEN = (1024);

// The stream information query is retried with a bigger buffer until it fits: bound the growth so a
// file with an unreasonable number of streams fails instead of looping until memory is exhausted.
static const DWORD FILEINFO_STREAMINFO_MAX_MULTIPLIER = 64;

namespace {

// Every entry names its stream as ":<name>:$DATA"; the type suffix is not part of the name we report.
constexpr std::wstring_view kDataSuffix = L":$DATA";
constexpr std::wstring_view kDefaultDataStream = L"::$DATA";

// Walk the FILE_STREAM_INFORMATION chain held in 'buffer', handing each entry's name to 'onName'.
// NtQueryInformationFile fills the buffer, but NextEntryOffset and StreamNameLength are read back out of
// it: the walk stops at the first entry whose header or name would reach past the bytes the query
// actually wrote, so a damaged or adversarial chain cannot step it outside them. A zero NextEntryOffset
// ends the chain, and a non-zero one always advances, so the walk terminates.
template <typename FnT>
void ForEachStreamName(const Orc::BufferView& buffer, FnT&& onName)
{
    constexpr size_t kHeaderSize = offsetof(FILE_STREAM_INFORMATION, StreamName);

    size_t offset = 0;
    for (;;)
    {
        if (!Orc::IsValidOffset(buffer, offset, kHeaderSize))
        {
            return;
        }

        const auto* entry = reinterpret_cast<const FILE_STREAM_INFORMATION*>(buffer.data() + offset);

        // StreamNameLength counts bytes, not characters
        const size_t nameBytes = entry->StreamNameLength;
        if (!Orc::IsValidOffset(buffer, offset + kHeaderSize, nameBytes))
        {
            return;
        }

        onName(std::wstring_view(
            reinterpret_cast<const wchar_t*>(buffer.data() + offset + kHeaderSize), nameBytes / sizeof(wchar_t)));

        if (entry->NextEntryOffset == 0)
        {
            return;
        }

        offset += entry->NextEntryOffset;
    }
}

}  // namespace

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
    if (pNtDll == nullptr)
    {
        Log::Debug("Could not load ntdll");
        return E_FAIL;
    }

    HRESULT hr = E_FAIL;
    if (FAILED(hr = CheckFileHandle()))
        return hr;

    DWORD dwMultiplier = 1L;

    do
    {
        const DWORD dwBufferLength = FILEINFO_STREAMINFO_BUFLEN * dwMultiplier;

        try
        {
            m_streamInformation.assign(dwBufferLength, 0);
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        IO_STATUS_BLOCK StatusBlock;
        hr = pNtDll->NtQueryInformationFile(
            m_hFile,
            &StatusBlock,
            m_streamInformation.data(),
            dwBufferLength,
            (FILE_INFORMATION_CLASS)FileStreamInformation);

        switch (hr)
        {
            case HRESULT_FROM_NT( 0L ):
                m_bStreamInformationAvailable = true;
                break;
            case HRESULT_FROM_NT(STATUS_BUFFER_OVERFLOW):
            case HRESULT_FROM_NT(STATUS_BUFFER_TOO_SMALL):
                m_streamInformation.clear();
                dwMultiplier++;
                if (dwMultiplier > FILEINFO_STREAMINFO_MAX_MULTIPLIER)
                {
                    // This is likely adversarial streams so log as critical
                    Log::Critical(
                        L"Stream information of '{}' does not fit in {} bytes, giving up",
                        m_szFullName,
                        FILEINFO_STREAMINFO_BUFLEN * FILEINFO_STREAMINFO_MAX_MULTIPLIER);
                    return hr;
                }

                break;
            default:
                return hr;
        }
    } while (m_streamInformation.empty());

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

    if (m_bStreamInformationAvailable && !m_streamInformation.empty())
    {
        // The names are accumulated straight into the result: the two passes the raw buffer needed - one to
        // total the lengths, one to concatenate into a malloc'd block - only existed to size that block.
        std::wstring names;
        ForEachStreamName(Orc::MakeBufferView(m_streamInformation), [&names](std::wstring_view name) {
            if (name == kDefaultDataStream)
            {
                return;  // the unnamed $DATA stream is not an alternate stream
            }

            if (name.size() >= kDataSuffix.size())
            {
                name.remove_suffix(kDataSuffix.size());
            }

            if (!names.empty())
            {
                names.push_back(L';');
            }

            names.append(name);
        });

        return output.WriteString(names);
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

USNRecordFileInfo::~USNRecordFileInfo(void) {}
