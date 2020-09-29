//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ObjectDirectory.h"

#include "NtDllExtension.h"

#include "SystemDetails.h"

#include "StructuredOutputWriter.h"
#include "TableOutputWriter.h"

#include "boost\scope_exit.hpp"

using namespace Orc;

static const NTSTATUS STATUS_NO_MORE_FILES = 0x80000006L;
static const NTSTATUS STATUS_BUFFER_TOO_SMALL = 0xC0000023L;

typedef struct _OBJECT_DIRECTORY_INFORMATION
{
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, *POBJECT_DIRECTORY_INFORMATION;

typedef struct _OBJECT_BASIC_INFORMATION
{
    ULONG Attributes;
    ACCESS_MASK GrantedAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    ULONG PagedPoolCharge;
    ULONG NonPagedPoolCharge;
    ULONG Reserved[3];
    ULONG NameInfoSize;
    ULONG TypeInfoSize;
    ULONG SecurityDescriptorSize;
    LARGE_INTEGER CreationTime;

} OBJECT_BASIC_INFORMATION, *POBJECT_BASIC_INFORMATION;

const FlagsDefinition ObjectDirectory::g_ObjectTypeDefinition[] = {
    {ObjectDirectory::Invalid, L"Invalid", L"Invalid object type"},
    {ObjectDirectory::Type, L"Type", L"Type object"},
    {ObjectDirectory::Directory, L"Directory", L"Directory object"},
    {ObjectDirectory::Session, L"Session", L"Session object"},
    {ObjectDirectory::WindowStations, L"WindowStations", L"WindowStations object"},
    {ObjectDirectory::Event, L"Event", L"Event object"},
    {ObjectDirectory::KeyedEvent, L"KeyedEvent", L"Keyed Event object"},
    {ObjectDirectory::Callback, L"Callback", L"Callback object"},
    {ObjectDirectory::Job, L"Job", L"Job object"},
    {ObjectDirectory::Mutant, L"Mutant", L"Mutant object"},
    {ObjectDirectory::Section, L"Section", L"Section object"},
    {ObjectDirectory::Semaphore, L"Semaphore", L"Semaphore object"},
    {ObjectDirectory::SymbolicLink, L"SymbolicLink", L"SymbolicLink object"},
    {ObjectDirectory::Device, L"Device", L"Device object"},
    {ObjectDirectory::Driver, L"Driver", L"Driver object"},
    {ObjectDirectory::ALPCPort, L"ALPC Port", L"ALPC Port object"},
    {ObjectDirectory::FilterConnectionPort, L"FilterConnectionPort", L"Filter Connection Port object"},
    {ObjectDirectory::Key, L"Key", L"Key object"},
    {ObjectDirectory::File, L"File", L"File object"},
    {0xFFFFFFFF, NULL, NULL}};

// constexpr auto FILE_OPEN_BY_FILE_ID                    = 0x00002000L;
// constexpr auto FILE_NON_DIRECTORY_FILE                 = 0x00000040L;
// constexpr auto FILE_RANDOM_ACCESS                      = 0x00000800L;
// constexpr auto FILE_SEQUENTIAL_ONLY                    = 0x00000004L;
// constexpr auto FILE_SYNCHRONOUS_IO_NONALERT            = 0x00000020L;
// constexpr auto FILE_SYNCHRONOUS_IO_ALERT               = 0x00000010L;
// constexpr auto FILE_OPEN_FOR_BACKUP_INTENT             = 0x00004000L;
// constexpr auto OBJ_CASE_INSENSITIVE                    = 0x00000040L;
constexpr auto STATUS_SUCCESS = 0x0;

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

using namespace std;

ObjectDirectory::ObjectType ObjectDirectory::GetObjectType(const UNICODE_STRING& type)
{
    UINT idx = 0L;

    while (g_ObjectTypeDefinition[idx].dwFlag != 0xFFFFFFFF)
    {

        if (!_wcsnicmp(type.Buffer, g_ObjectTypeDefinition[idx].szShortDescr, type.Length / sizeof(WCHAR)))
            return (ObjectDirectory::ObjectType)g_ObjectTypeDefinition[idx].dwFlag;

        idx++;
    }
    return ObjectType::Invalid;
}

ObjectDirectory::ObjectType ObjectDirectory::GetObjectType(const std::wstring& type)
{
    UINT idx = 0L;

    while (g_ObjectTypeDefinition[idx].dwFlag != 0xFFFFFFFF)
    {

        if (!_wcsnicmp(type.data(), g_ObjectTypeDefinition[idx].szShortDescr, type.size()))
            return (ObjectDirectory::ObjectType)g_ObjectTypeDefinition[idx].dwFlag;

        idx++;
    }
    return ObjectType::Invalid;
}

HRESULT ObjectDirectory::ObjectInstance::Write(ITableOutput& output, const std::wstring& strDescription) const
{
    SystemDetails::WriteComputerName(output);
    SystemDetails::WriteDescriptionString(output);

    output.WriteExactFlags(Type, ObjectDirectory::g_ObjectTypeDefinition);
    output.WriteString(Name);
    output.WriteString(Path);
    if (LinkTarget.empty())
    {
        output.WriteNothing();
        output.WriteNothing();
    }
    else
    {
        output.WriteString(LinkTarget);
        output.WriteFileTime(LinkCreationTime.QuadPart);
    }
    output.WriteString(strDescription);
    output.WriteEndOfLine();
    return S_OK;
}

HRESULT ObjectDirectory::ObjectInstance::Write(

    IStructuredOutput& pWriter,
    LPCWSTR szElement) const
{
    pWriter.BeginElement(szElement);

    pWriter.WriteNamed(L"type", (DWORD) Type, ObjectDirectory::g_ObjectTypeDefinition);
    pWriter.WriteNamed(L"name", Name.c_str());

    if (!Path.empty())
        pWriter.WriteNamed(L"path", Path.c_str());
    if (!LinkTarget.empty())
        pWriter.WriteNamed(L"link_target", LinkTarget.c_str());
    if (LinkCreationTime.QuadPart != 0LL)
        pWriter.WriteNamedFileTime(L"link_creationtime", LinkCreationTime.QuadPart);

    pWriter.EndElement(szElement);
    return S_OK;
}

HRESULT ObjectDirectory::ParseObjectDirectory(
    const std::wstring& aObjDir,
    std::vector<ObjectInstance>& objects,
    bool bRecursive)
{
    HRESULT hr;

    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();
    if (pNtDll == nullptr)
        return E_FAIL;

    HANDLE hRoot = INVALID_HANDLE_VALUE;

    OBJECT_ATTRIBUTES ObjAttr;
    UNICODE_STRING sFileID;

    pNtDll->RtlInitUnicodeString(&sFileID, aObjDir.c_str());

    InitializeObjectAttributes(&ObjAttr, &sFileID, OBJ_PERMANENT, NULL, NULL);

    if (FAILED(hr = pNtDll->NtOpenDirectoryObject(&hRoot, GENERIC_READ, &ObjAttr)))
    {
        spdlog::error(L"Failed to open object directory '{}' (code: {:#x})", aObjDir, hr);
        return hr;
    }
    BOOST_SCOPE_EXIT((&hRoot)) { CloseHandle(hRoot); }
    BOOST_SCOPE_EXIT_END;

    DWORD dwPageSize = 0L;
    if (FAILED(hr = SystemDetails::GetPageSize(dwPageSize)))
        return hr;
    CBinaryBuffer ObjInformation(true);
    if (!ObjInformation.SetCount(dwPageSize * 4))
        return E_OUTOFMEMORY;

    ULONG Context = 0L, returnedLength = 0L;
    while (SUCCEEDED(
        hr = pNtDll->NtQueryDirectoryObject(
            hRoot,
            ObjInformation.GetData(),
            (ULONG)ObjInformation.GetCount(),
            FALSE,
            FALSE,
            &Context,
            &returnedLength)))
    {
        POBJECT_DIRECTORY_INFORMATION pObjInfo = (POBJECT_DIRECTORY_INFORMATION)ObjInformation.GetData();

        DWORD idx = 0L;

        while (idx < returnedLength)
        {
            spdlog::debug(
                L"Type: {}, Name: {}",
                std::wstring_view(pObjInfo->TypeName.Buffer, pObjInfo->TypeName.Length / sizeof(WCHAR)),
                std::wstring_view(pObjInfo->Name.Buffer, pObjInfo->Name.Length / sizeof(WCHAR)));

            ObjectType type = GetObjectType(pObjInfo->TypeName);

            std::wstring path;

            if (aObjDir.size() == 1 && aObjDir[0] == L'\\')
            {
                path.append(aObjDir);
            }
            else if (!aObjDir.empty())
            {
                path.reserve(aObjDir.size() + 1 + pObjInfo->Name.Length / sizeof(WCHAR) + 1);
                path.append(aObjDir);
                path.append(L"\\", 1);
            }

            path.append(pObjInfo->Name.Buffer, pObjInfo->Name.Length / sizeof(WCHAR));

            switch (type)
            {
                case ObjectType::SymbolicLink:
                {
                    HANDLE hSymLink = INVALID_HANDLE_VALUE;

                    OBJECT_ATTRIBUTES SymLinkAttr;

                    InitializeObjectAttributes(&SymLinkAttr, &pObjInfo->Name, OBJ_PERMANENT, hRoot, NULL);

                    if (FAILED(pNtDll->NtOpenSymbolicLinkObject(&hSymLink, GENERIC_READ, &SymLinkAttr)))
                        break;

                    BOOST_SCOPE_EXIT((&hSymLink)) { CloseHandle(hSymLink); }
                    BOOST_SCOPE_EXIT_END;

                    UNICODE_STRING LinkTarget;
                    if (FAILED(pNtDll->RtlInitUnicodeString(&LinkTarget, L"")))
                        break;

                    ULONG ulReturnedLength = 0L;
                    if (FAILED(hr = pNtDll->NtQuerySymbolicLinkObject(hSymLink, &LinkTarget, &ulReturnedLength)))
                    {
                        if (hr != HRESULT_FROM_NT(STATUS_BUFFER_TOO_SMALL))
                            break;
                    }

                    CBinaryBuffer buffer;
                    if (!buffer.SetCount(ulReturnedLength))
                        return E_OUTOFMEMORY;

                    LinkTarget.Buffer = (PWSTR)buffer.GetData();
                    LinkTarget.MaximumLength = (USHORT)ulReturnedLength;
                    LinkTarget.Length = 0L;
                    if (FAILED(hr = pNtDll->NtQuerySymbolicLinkObject(hSymLink, &LinkTarget, &ulReturnedLength)))
                        break;

                    OBJECT_BASIC_INFORMATION obi;
                    ZeroMemory(&obi, sizeof(OBJECT_BASIC_INFORMATION));

                    if (FAILED(pNtDll->NtQueryObject(
                            hSymLink,
                            ObjectBasicInformation,
                            &obi,
                            (ULONG)sizeof(OBJECT_BASIC_INFORMATION),
                            &ulReturnedLength)))
                        break;

                    objects.emplace_back(
                        type,
                        std::wstring(pObjInfo->Name.Buffer, pObjInfo->Name.Length / sizeof(WCHAR)),
                        std::move(path),
                        std::wstring(LinkTarget.Buffer, LinkTarget.Length / sizeof(WCHAR)),
                        obi.CreationTime);
                }
                break;
                case Invalid:
                    break;
                case Directory:
                {
                    if (bRecursive)
                    {
                        if (FAILED(hr = ParseObjectDirectory(path, objects, bRecursive)))
                        {
                            spdlog::warn(
                                L"Failed to recursively parse directory '{}' (subdir '{}') (code: {:#x})",
                                aObjDir,
                                path,
                                hr);
                        }
                    }

                    objects.emplace_back(
                        type,
                        std::wstring(pObjInfo->Name.Buffer, pObjInfo->Name.Length / sizeof(WCHAR)),
                        std::move(path));
                }
                break;
                default:
                {
                    objects.emplace_back(
                        type,
                        std::wstring(pObjInfo->Name.Buffer, pObjInfo->Name.Length / sizeof(WCHAR)),
                        std::move(path));
                }
            }

            pObjInfo++;

            if (pObjInfo->Name.Buffer == NULL)
                break;

            idx += sizeof(OBJECT_DIRECTORY_INFORMATION);
        }
    }

    return S_OK;
}

ObjectDirectory::~ObjectDirectory() {}
