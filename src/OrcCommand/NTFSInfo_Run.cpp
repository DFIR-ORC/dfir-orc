//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include <strsafe.h>

#include <boost/scope_exit.hpp>
#include <boost/algorithm/string/join.hpp>

#include <Sddl.h>

#include "NTFSInfo.h"

#include "USNJournalWalker.h"
#include "USNRecordFileInfo.h"
#include "MFTRecordFileInfo.h"
#include "MountedVolumeReader.h"
#include "MFTWalker.h"
#include "SystemDetails.h"
#include "SnapshotVolumeReader.h"
#include "ParameterCheck.h"
#include "Privilege.h"
#include "EmbeddedResource.h"
#include "Output/Text/Print/Location.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::NTFSInfo;

HRESULT Main::RunThroughUSNJournal()
{
    HRESULT hr = E_FAIL;

    dwTotalFileTreated = 0L;

    const auto& locs = config.locs.GetAltitudeLocations();
    std::vector<std::shared_ptr<Location>> locations;

    std::copy_if(begin(locs), end(locs), back_inserter(locations), [](const shared_ptr<Location>& loc) {
        if (loc == nullptr)
            return false;

        return (loc->GetParse() && loc->IsNTFS());
    });

    std::shared_ptr<TableOutput::IWriter> pFileInfoWriter;

    if (config.outFileInfo.Type == OutputSpec::Kind::Archive || config.outFileInfo.Type == OutputSpec::Kind::Directory)
    {
        pFileInfoWriter = TableOutput::GetWriter(L"NTFSInfo_usn", config.outFileInfo);
        if (nullptr == pFileInfoWriter)
        {
            Log::Error("Failed to create output file information file");
            return E_FAIL;
        }
    }
    else
    {
        pFileInfoWriter = TableOutput::GetWriter(config.outFileInfo);
        if (nullptr == pFileInfoWriter)
        {
            Log::Error("Failed to create output file information file");
            return E_FAIL;
        }
    }

    IUSNJournalWalker::Callbacks callbacks;

    callbacks.ProgressCallback = [this](const ULONG dwProgress) { DisplayProgress(dwProgress); };

    callbacks.RecordCallback = [this, pFileInfoWriter](
                                   std::shared_ptr<VolumeReader>& volreader, WCHAR* szFullName, USN_RECORD* pElt) {
        std::shared_ptr<MountedVolumeReader> mountedVolReader = dynamic_pointer_cast<MountedVolumeReader>(volreader);

        if (mountedVolReader == nullptr)
        {
            return;
        }

        try
        {
            USNRecordFileInfo fi(
                m_utilitiesConfig.strComputerName.c_str(),
                volreader,
                config.DefaultIntentions,
                config.Filters,
                szFullName,
                pElt,
                m_codeVerifier);

            HRESULT hr = fi.WriteFileInformation(NtfsFileInfo::g_NtfsColumnNames, *pFileInfoWriter, config.Filters);
            if (FAILED(hr))
            {
                Log::Error(L"Could not WriteFileInformation for '{}'", szFullName);
            }
        }
        catch (WCHAR* e)
        {
            Log::Error(L"Could not WriteFileInformation for '{}': {}", szFullName, e);
        }
    };

    for (const auto& loc : locations)
    {
        if (loc->GetType() != Location::Type::MountedVolume)
        {
            Log::Warn(
                L"Location '{}' is not available with USN walker (should be a mounted volume)", loc->GetLocation());
            continue;
        }

        m_console.Print(L"Parsing: {} [{}]", loc->GetLocation(), boost::join(loc->GetPaths(), L", "));

        if (config.outFileInfo.Type == OutputSpec::Kind::Directory)
        {
            WCHAR szOutputFile[MAX_PATH];
            StringCchPrintf(szOutputFile, MAX_PATH, L"NTFSInfo_%s_.csv", loc->GetIdentifier().c_str());
            if (nullptr == (pFileInfoWriter = TableOutput::GetWriter(szOutputFile, config.outFileInfo)))
            {
                Log::Error("Failed to create output file information file");
                continue;
            }
        }

        USNJournalWalker walk;
        HRESULT hr = E_FAIL;

        if (FAILED(hr = walk.Initialize(loc)))
        {
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_SYSTEM_LIMITATION))
            {
                Log::Warn(L"File system not eligible for '{}'", loc->GetLocation());
            }
            else
            {
                Log::Warn(L"Failed to init walk for '{}' (code: {:#x})", loc->GetLocation(), hr);
            }
        }
        else
        {
            if (FAILED(hr = walk.EnumJournal(callbacks)))
            {
                Log::Error(L"Failed to walk volume '{}' (code: {:#x})", loc->GetLocation(), hr);
            }
            else
            {
                Log::Info(L"Done");
            }
        }

        if (config.outFileInfo.Type == OutputSpec::Kind::Directory)
        {
            pFileInfoWriter->Close();
            pFileInfoWriter.reset();
        }
    }

    return S_OK;
}

// MFT Walker call backs
void Main::DisplayProgress(const ULONG dwProgress)
{
    if (dwProgress == 0L)
    {
        m_dwProgress = 0L;
    }

    if (m_dwProgress != dwProgress)
    {
        if (m_dwProgress % 2 == 0)
        {
            m_console.Write(L".");
        }

        m_dwProgress = dwProgress;
    }
}

void Main::FileAndDataInformation(
    ITableOutput& output,
    const std::shared_ptr<VolumeReader>& volreader,
    MFTRecord* pElt,
    const PFILE_NAME pFileName,
    const std::shared_ptr<DataAttribute>& pDataAttr)
{
    try
    {
        const WCHAR* szFullName = m_FullNameBuilder(pFileName, pDataAttr);

        MFTRecordFileInfo fi(
            m_utilitiesConfig.strComputerName,
            volreader,
            config.DefaultIntentions,
            config.Filters,
            szFullName,
            pElt,
            pFileName,
            pDataAttr,
            m_codeVerifier);

        HRESULT hr = fi.WriteFileInformation(NtfsFileInfo::g_NtfsColumnNames, output, config.Filters);
        ++dwTotalFileTreated;
    }
    catch (WCHAR* e)
    {
        Log::Error(L"Exception: could not WriteFileInformation: {}", e);
    }
}

void Main::DirectoryInformation(
    ITableOutput& output,
    const std::shared_ptr<VolumeReader>& volreader,
    MFTRecord* pElt,
    const PFILE_NAME pFileName,
    const std::shared_ptr<IndexAllocationAttribute>& pAttr)
{
    try
    {
        const WCHAR* szFullName = m_FullNameBuilder(pFileName, nullptr);

        MFTRecordFileInfo fi(
            m_utilitiesConfig.strComputerName,
            volreader,
            config.DefaultIntentions,
            config.Filters,
            szFullName,
            pElt,
            pFileName,
            nullptr,
            m_codeVerifier);

        HRESULT hr = fi.WriteFileInformation(NtfsFileInfo::g_NtfsColumnNames, output, config.Filters);
        ++dwTotalFileTreated;
    }
    catch (WCHAR* e)
    {
        Log::Error(L"Exception: could not WriteFileInformation: {}", e);
    }
}

HRESULT Main::WriteTimeLineEntry(
    ITableOutput& timelineOutput,
    const std::shared_ptr<VolumeReader>& volreader,
    MFTRecord* pElt,
    const PFILE_NAME pFileName,
    DWORD dwKind,
    LONGLONG llTime)
{
    return WriteTimeLineEntry(timelineOutput, volreader, pElt, pFileName, dwKind, *((FILETIME*)&llTime));
}

HRESULT Main::WriteTimeLineEntry(
    ITableOutput& timelineOutput,
    const std::shared_ptr<VolumeReader>& volreader,
    MFTRecord* pElt,
    const PFILE_NAME pFileName,
    DWORD dwKind,
    FILETIME llTime)
{
    timelineOutput.WriteString(m_utilitiesConfig.strComputerName.c_str());

    timelineOutput.WriteInteger(volreader->VolumeSerialNumber());

    static const Orc::FlagsDefinition KindOfDateDefs[] = {
        {InvalidKind, L"InvalidKind", L"InvalidKind"},
        {CreationTime, L"CreationTime", L"CreationTime"},
        {LastModificationTime, L"LastModificationTime", L"LastModificationTime"},
        {LastAccessTime, L"LastAccessTime", L"LastAccessTime"},
        {LastChangeTime, L"LastChangeTime", L"LastChangeTime"},
        {FileNameCreationDate, L"FileNameCreationDate", L"FileNameCreationDate"},
        {FileNameLastModificationDate, L"FileNameLastModificationDate", L"FileNameLastModificationDate"},
        {FileNameLastAccessDate, L"FileNameLastAccessDate", L"FileNameLastAccessDate"},
        {FileNameLastAttrModificationDate, L"FileNameLastAttrModificationDate", L"FileNameLastAttrModificationDate"},
        {0xFFFFFFFF, L"TheWorldEndsHere", L"TheWorldEndsHere"}};

    timelineOutput.WriteFlags(dwKind, KindOfDateDefs, L',');
    timelineOutput.WriteFileTime(llTime);
    timelineOutput.WriteInteger(pElt->GetSafeMFTSegmentNumber());

    const auto& attrs = pElt->GetAttributeList();
    auto usInstanceID = (USHORT)-1;
    std::for_each(begin(attrs), end(attrs), [pFileName, &usInstanceID](const AttributeListEntry& attr) {
        if (attr.TypeCode() == $FILE_NAME)
        {
            auto attribute = attr.Attribute();
            if (attribute != nullptr)
            {
                auto header = (PATTRIBUTE_RECORD_HEADER)attribute->Header();
                if (pFileName == (PFILE_NAME)((LPBYTE)header + header->Form.Resident.ValueOffset))
                {
                    usInstanceID = header->Instance;
                }
            }
        }
    });
    if (usInstanceID == (USHORT)-1)
        timelineOutput.WriteNothing();
    else
        timelineOutput.WriteInteger((DWORD)usInstanceID);

    auto snapshot_reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(volreader);

    if (snapshot_reader)
        timelineOutput.WriteGUID(snapshot_reader->GetSnapshotID());
    else
        timelineOutput.WriteGUID(GUID_NULL);

    timelineOutput.WriteEndOfLine();
    return S_OK;
}

void Main::ElementInformation(ITableOutput& output, const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt)
{
    PSTANDARD_INFORMATION pInfo = pElt->GetStandardInformation();
    if (pInfo == nullptr)
        return;
    WriteTimeLineEntry(output, volreader, pElt, nullptr, CreationTime, pInfo->CreationTime);
    WriteTimeLineEntry(output, volreader, pElt, nullptr, LastModificationTime, pInfo->LastModificationTime);
    WriteTimeLineEntry(output, volreader, pElt, nullptr, LastAccessTime, pInfo->LastAccessTime);
    WriteTimeLineEntry(output, volreader, pElt, nullptr, LastChangeTime, pInfo->LastChangeTime);
}

void Main::TimelineInformation(
    ITableOutput& output,
    const std::shared_ptr<VolumeReader>& volreader,
    MFTRecord* pElt,
    const PFILE_NAME pFileName)
{
    WriteTimeLineEntry(output, volreader, pElt, pFileName, FileNameCreationDate, pFileName->Info.CreationTime);
    WriteTimeLineEntry(
        output, volreader, pElt, pFileName, FileNameLastModificationDate, pFileName->Info.LastModificationTime);
    WriteTimeLineEntry(output, volreader, pElt, pFileName, FileNameLastAccessDate, pFileName->Info.LastAccessTime);
    WriteTimeLineEntry(
        output, volreader, pElt, pFileName, FileNameLastAttrModificationDate, pFileName->Info.LastChangeTime);
}

void Main::SecurityDescriptorInformation(
    ITableOutput& output,
    const std::shared_ptr<VolumeReader>& volreader,
    const PSECURITY_DESCRIPTOR_ENTRY pEntry)
{
    output.WriteString(m_utilitiesConfig.strComputerName.c_str());
    output.WriteInteger(volreader->VolumeSerialNumber());
    output.WriteInteger((DWORD)pEntry->SecID);
    output.WriteInteger((DWORD)pEntry->Hash);

    LPWSTR szSDDL = nullptr;

    SECURITY_INFORMATION InfoFlags = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION
        | SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION
        | PROTECTED_DACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION
        | UNPROTECTED_DACL_SECURITY_INFORMATION;

    if (!ConvertSecurityDescriptorToStringSecurityDescriptor(
            &pEntry->SecurityDescriptor, SDDL_REVISION_1, InfoFlags, &szSDDL, NULL))
    {
        Log::Debug("Failed to convert security descriptor to SDDL (code: {:#x}))", HRESULT_FROM_WIN32(GetLastError()));
        output.WriteNothing();
        if (szSDDL != nullptr)
        {
            LocalFree(szSDDL);
            szSDDL = nullptr;
        }
    }
    else
    {
        output.WriteString(szSDDL);
    }

    if (szSDDL != nullptr)
    {
        DWORD dwSecDescrLength = GetSecurityDescriptorLength(&pEntry->SecurityDescriptor);
        output.WriteInteger(dwSecDescrLength);

        PSECURITY_DESCRIPTOR pNormalisedSecDescr = nullptr;
        ULONG ulNormalisedSecDescrLength = 0L;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
                szSDDL, SDDL_REVISION_1, &pNormalisedSecDescr, &ulNormalisedSecDescrLength))
        {
            Log::Debug(
                "Failed to convert SDDL to security descriptor (code: {:#x})", HRESULT_FROM_WIN32(GetLastError()));
            output.WriteNothing();
        }
        else
        {
            LocalFree(pNormalisedSecDescr);
            output.WriteInteger(ulNormalisedSecDescrLength);
        }
        LocalFree(szSDDL);
        szSDDL = nullptr;
    }
    else
    {
        output.WriteNothing();
        output.WriteNothing();
    }

    output.WriteInteger(
        (DWORD)pEntry->SizeEntry - (sizeof(SECURITY_DESCRIPTOR_ENTRY) - sizeof(SECURITY_DESCRIPTOR_RELATIVE)));

    auto snapshot_reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(volreader);

    if (snapshot_reader)
        output.WriteGUID(snapshot_reader->GetSnapshotID());
    else
        output.WriteGUID(GUID_NULL);

    output.WriteEndOfLine();
    return;
}

void Main::AttrInformation(
    ITableOutput& output,
    const std::shared_ptr<VolumeReader>& volreader,
    MFTRecord* pElt,
    const AttributeListEntry& Attr)
{
    static const FlagsDefinition AttrTypeDefs[] = {
        {$UNUSED, L"$UNUSED", L"$UNUSED"},
        {$STANDARD_INFORMATION, L"$STANDARD_INFORMATION", L"$STANDARD_INFORMATION"},
        {$ATTRIBUTE_LIST, L"$ATTRIBUTE_LIST", L"$ATTRIBUTE_LIST"},
        {$FILE_NAME, L"$FILE_NAME", L"$FILE_NAME"},
        {$OBJECT_ID, L"$OBJECT_ID", L"$OBJECT_ID"},
        {$SECURITY_DESCRIPTOR, L"$SECURITY_DESCRIPTOR", L"$SECURITY_DESCRIPTOR"},
        {$VOLUME_NAME, L"$VOLUME_NAME", L"$VOLUME_NAME"},
        {$VOLUME_INFORMATION, L"$VOLUME_INFORMATION", L"$VOLUME_INFORMATION"},
        {$DATA, L"$DATA", L"$DATA"},
        {$INDEX_ROOT, L"$INDEX_ROOT", L"$INDEX_ROOT"},
        {$INDEX_ALLOCATION, L"$INDEX_ALLOCATION", L"$INDEX_ALLOCATION"},
        {$BITMAP, L"$BITMAP", L"$BITMAP"},
        {$REPARSE_POINT, L"$REPARSE_POINT", L"$REPARSE_POINT"},
        {$EA_INFORMATION, L"$EA_INFORMATION", L"$EA_INFORMATION"},
        {$EA, L"$EA", L"$EA"},
        {$LOGGED_UTILITY_STREAM, L"$LOGGED_UTILITY_STREAM", L"$LOGGED_UTILITY_STREAM"},
        {$FIRST_USER_DEFINED_ATTRIBUTE, L"$FIRST_USER_DEFINED_ATTRIBUTE", L"$FIRST_USER_DEFINED_ATTRIBUTE"},
        {$END, L"$END", L"$END"}};

    static const FlagsDefinition FormTypeDefs[] = {
        {RESIDENT_FORM, L"Resident", L"Resident"},
        {NONRESIDENT_FORM, L"NonResident", L"NonResident"},
        {0xFFFFFFFF, L"$END", L"$END"}};

    output.WriteString(m_utilitiesConfig.strComputerName.c_str());

    output.WriteInteger(volreader->VolumeSerialNumber());

    output.WriteInteger(pElt->GetSafeMFTSegmentNumber());
    output.WriteInteger(Attr.HostRecordSegmentNumber());

    output.WriteExactFlags(Attr.TypeCode(), AttrTypeDefs);

    output.WriteString(std::wstring_view(Attr.AttributeName(), (int)Attr.AttributeNameLength()));

    output.WriteExactFlags(Attr.FormCode(), FormTypeDefs);

    if (Attr.Attribute() != nullptr)
    {
        DWORDLONG dwlDataSize = 0;
        if (SUCCEEDED(Attr.Attribute()->DataSize(volreader, dwlDataSize)))
        {
            output.WriteFileSize(dwlDataSize);
        }
        else
        {
            output.WriteFileSize(0);
        }
    }
    else
    {
        output.WriteFileSize(0);
    }

    output.WriteInteger((DWORD)Attr.Flags());
    output.WriteInteger((DWORD)Attr.Instance());

    output.WriteInteger((DWORD)pElt->GetAttributeIndex(Attr.Attribute()));

    output.WriteInteger(Attr.LowestVCN());

    auto snapshot_reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(volreader);

    if (snapshot_reader)
        output.WriteGUID(snapshot_reader->GetSnapshotID());
    else
        output.WriteGUID(GUID_NULL);

    output.WriteEndOfLine();
}

void Main::I30Information(
    ITableOutput& output,
    const std::shared_ptr<VolumeReader>& volreader,
    MFTRecord* pElt,
    const PINDEX_ENTRY pEntry,
    PFILE_NAME pFileName,
    bool bCarvedEntry)
{
    output.WriteString(m_utilitiesConfig.strComputerName.c_str());
    output.WriteInteger(volreader->VolumeSerialNumber());
    output.WriteBool(bCarvedEntry);
    output.WriteInteger((MFTUtils::SafeMFTSegmentNumber)NtfsFullSegmentNumber(&pEntry->FileReference));
    output.WriteInteger((MFTUtils::SafeMFTSegmentNumber)NtfsFullSegmentNumber(&pFileName->ParentDirectory));
    output.WriteString(std::wstring_view(pFileName->FileName, (int)pFileName->FileNameLength));
    output.WriteInteger((DWORD)pFileName->Flags);
    output.WriteFileTime(pFileName->Info.CreationTime);
    output.WriteFileTime(pFileName->Info.LastModificationTime);
    output.WriteFileTime(pFileName->Info.LastAccessTime);
    output.WriteFileTime(pFileName->Info.LastChangeTime);

    auto snapshot_reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(volreader);

    if (snapshot_reader)
        output.WriteGUID(snapshot_reader->GetSnapshotID());
    else
        output.WriteGUID(GUID_NULL);

    output.WriteEndOfLine();
}

HRESULT Main::Prepare()
{
    HRESULT hr = E_FAIL;

    if (config.outFileInfo.Type == OutputSpec::Kind::Archive)
    {
        if (FAILED(hr = m_FileInfoOutput.Prepare(config.outFileInfo)))
        {
            Log::Error(L"Failed to prepare archive for '{}' (code: {:#x})", config.outFileInfo.Path, hr);
            return hr;
        }
    }

    if (config.outAttrInfo.Type == OutputSpec::Kind::Archive)
    {
        if (FAILED(hr = m_AttrOutput.Prepare(config.outAttrInfo)))
        {
            Log::Error(L"Failed to prepare archive for '{}' (code: {:#x})", config.outAttrInfo.Path, hr);
            return hr;
        }
    }

    if (config.outI30Info.Type == OutputSpec::Kind::Archive)
    {
        if (FAILED(hr = m_I30Output.Prepare(config.outI30Info)))
        {
            Log::Error(L"Failed to prepare archive for '{}' (code: {:#x})", config.outI30Info.Path, hr);
            return hr;
        }
    }

    if (config.outTimeLine.Type == OutputSpec::Kind::Archive)
    {
        if (FAILED(hr = m_TimeLineOutput.Prepare(config.outTimeLine)))
        {
            Log::Error(L"Failed to prepare archive for '{}' (code: {:#x})", config.outTimeLine.Path, hr);
            return hr;
        }
    }

    if (config.outSecDescrInfo.Type == OutputSpec::Kind::Archive)
    {
        if (FAILED(hr = m_SecDescrOutput.Prepare(config.outSecDescrInfo)))
        {
            Log::Error(L"Failed to prepare archive for '{}' (code: {:#x})", config.outSecDescrInfo.Path, hr);
            return hr;
        }
    }

    return S_OK;
}

HRESULT Main::GetWriters(std::vector<std::shared_ptr<Location>>& locations)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_FileInfoOutput.GetWriters(config.outFileInfo, L"NTFSInfo", locations)))
    {
        Log::Error("Failed to create file information writers (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = m_AttrOutput.GetWriters(config.outAttrInfo, L"AttrInfo", locations)))
    {
        Log::Error("Failed to create attribute information writers (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = m_I30Output.GetWriters(config.outI30Info, L"I30Info", locations)))
    {
        Log::Error("Failed to create I30 information writers (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = m_TimeLineOutput.GetWriters(config.outTimeLine, L"NTFSTimeLine", locations)))
    {
        Log::Error("Failed to create timeline information writers (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = m_SecDescrOutput.GetWriters(config.outSecDescrInfo, L"SecDescr", locations)))
    {
        Log::Error("Failed to create security descriptors information writers (code: {:#x})", hr);
        return hr;
    }
    return S_OK;
}

HRESULT Main::RunThroughMFT()
{
    auto output = m_console.OutputTree();
    HRESULT hr = E_FAIL;

    const auto& locs = config.locs.GetAltitudeLocations();
    std::vector<std::shared_ptr<Location>> locations;
    std::vector<std::shared_ptr<Location>> allLocations;

    std::copy_if(begin(locs), end(locs), back_inserter(locations), [](const shared_ptr<Location>& loc) {
        if (loc == nullptr)
            return false;

        return (loc->GetParse() && loc->IsNTFS());
    });

    std::copy_if(begin(locs), end(locs), back_inserter(allLocations), [](const std::shared_ptr<Location>& loc) {
        if (loc == nullptr)
            return false;

        return (loc->IsNTFS());
    });

    BOOST_SCOPE_EXIT(&config, &m_FileInfoOutput, &m_AttrOutput, &m_I30Output, &m_TimeLineOutput, &m_SecDescrOutput)
    {
        m_FileInfoOutput.CloseAll(config.outFileInfo);
        m_AttrOutput.CloseAll(config.outAttrInfo);
        m_I30Output.CloseAll(config.outI30Info);
        m_TimeLineOutput.CloseAll(config.outTimeLine);
        m_SecDescrOutput.CloseAll(config.outSecDescrInfo);
    }
    BOOST_SCOPE_EXIT_END;

    if (config.outFileInfo.Type == OutputSpec::Kind::Archive || config.outFileInfo.Type == OutputSpec::Kind::Directory)
    {
        if (FAILED(hr = Prepare()))
        {
            Log::Error("Failed to prepare outputs (code: {:#x})", hr);
            return hr;
        }

        m_FileInfoOutput.WriteVolStats(config.volumesStatsOutput, allLocations);
    }

    if (FAILED(hr = GetWriters(locations)))
    {
        Log::Error("Failed to create writers for NTFSInfo (code: {:#x})", hr);
        return hr;
    }

    auto fileinfoIterator = begin(m_FileInfoOutput.Outputs());
    auto attrIterator = begin(m_AttrOutput.Outputs());
    auto i30Iterator = begin(m_I30Output.Outputs());
    auto timelineIterator = begin(m_TimeLineOutput.Outputs());
    auto secdescrIterator = begin(m_SecDescrOutput.Outputs());

    for (auto& loc : locations)
    {
        BOOST_SCOPE_EXIT(
            &config,
            &m_FileInfoOutput,
            &fileinfoIterator,
            &m_AttrOutput,
            &attrIterator,
            &m_I30Output,
            &i30Iterator,
            &m_TimeLineOutput,
            &timelineIterator,
            &m_SecDescrOutput,
            &secdescrIterator)
        {
            m_FileInfoOutput.CloseOne(config.outFileInfo, *fileinfoIterator);
            fileinfoIterator++;
            m_AttrOutput.CloseOne(config.outAttrInfo, *attrIterator);
            attrIterator++;
            m_I30Output.CloseOne(config.outI30Info, *i30Iterator);
            i30Iterator++;
            m_TimeLineOutput.CloseOne(config.outTimeLine, *timelineIterator);
            timelineIterator++;
            m_SecDescrOutput.CloseOne(config.outSecDescrInfo, *secdescrIterator);
            secdescrIterator++;
        }
        BOOST_SCOPE_EXIT_END;

        output.Add(L"Parsing: {} [{}]", loc->GetLocation(), boost::join(loc->GetPaths(), L", "));

        MFTWalker::Callbacks callBacks;

        if (fileinfoIterator->second != nullptr)
        {
            callBacks.FileNameAndDataCallback = [this, fileinfoIterator](
                                                    const std::shared_ptr<VolumeReader>& volreader,
                                                    MFTRecord* pElt,
                                                    const PFILE_NAME pFileName,
                                                    const std::shared_ptr<DataAttribute>& pDataAttr) {
                FileAndDataInformation(*fileinfoIterator->second, volreader, pElt, pFileName, pDataAttr);
            };
            callBacks.DirectoryCallback = [this, fileinfoIterator](
                                              const std::shared_ptr<VolumeReader>& volreader,
                                              MFTRecord* pElt,
                                              const PFILE_NAME pFileName,
                                              const std::shared_ptr<IndexAllocationAttribute>& pAttr) {
                DirectoryInformation(*fileinfoIterator->second, volreader, pElt, pFileName, pAttr);
            };
        }
        if (timelineIterator->second != nullptr)
        {
            callBacks.ElementCallback =
                [this, timelineIterator](const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt) {
                    ElementInformation(*timelineIterator->second, volreader, pElt);
                };
            callBacks.FileNameCallback =
                [this, timelineIterator](
                    const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt, const PFILE_NAME pFileName) {
                    TimelineInformation(*timelineIterator->second, volreader, pElt, pFileName);
                };
        }

        if (attrIterator->second != nullptr)
        {
            callBacks.AttributeCallback = [this, attrIterator](
                                              const std::shared_ptr<VolumeReader>& volreader,
                                              MFTRecord* pElt,
                                              const AttributeListEntry& AttrEntry) {
                AttrInformation(*attrIterator->second, volreader, pElt, AttrEntry);
            };
        }

        if (i30Iterator->second != nullptr)
        {
            callBacks.I30Callback = [this, i30Iterator](
                                        const std::shared_ptr<VolumeReader>& volreader,
                                        MFTRecord* pElt,
                                        const PINDEX_ENTRY& pEntry,
                                        const PFILE_NAME pFileName,
                                        bool bCarvedEntry) {
                I30Information(*i30Iterator->second, volreader, pElt, pEntry, pFileName, bCarvedEntry);
            };
        }

        if (secdescrIterator->second != nullptr)
        {
            callBacks.SecDescCallback = [this, secdescrIterator](
                                            const std::shared_ptr<VolumeReader>& volreader,
                                            const PSECURITY_DESCRIPTOR_ENTRY pEntry) {
                SecurityDescriptorInformation(*secdescrIterator->second, volreader, pEntry);
            };
        }

        callBacks.ProgressCallback = [this](const ULONG dwProgress) -> HRESULT {
            DisplayProgress(dwProgress);
            return S_OK;
        };

        MFTWalker walker;
        HRESULT hr = E_FAIL;

        if (FAILED(hr = walker.Initialize(loc, (bool)config.bResurrectRecords)))
        {
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_SYSTEM_LIMITATION))
            {
                Log::Warn(L"File system not eligible for '{}'", loc->GetLocation());
            }
            else
            {
                Log::Error(L"Failed to init walk for '{}' (code: {:#x})", loc->GetLocation(), hr);
            }
        }
        else
        {
            m_FullNameBuilder = walker.GetFullNameBuilder();
            if (FAILED(hr = walker.Walk(callBacks)))
            {
                Log::Error(L"Failed to walk volume '{}' (code: {:#x})", loc->GetLocation(), hr);
            }
            else
            {
                m_console.Print("Done");
                walker.Statistics(L"");
            }
        }
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadWinTrust()))
        return hr;

    try
    {
        if (!config.strWalker.compare(L"USN"))
        {
            if (FAILED(hr = RunThroughUSNJournal()))
            {
                Log::Error("Failed to enumerate the USNs (code: {:#x})", hr);
                return hr;
            }
        }
        else if (!config.strWalker.compare(L"MFT"))
        {
            if (FAILED(hr = RunThroughMFT()))
            {
                Log::Error("Failed to enumerate the MFT records (code: {:#x})", hr);
                return hr;
            }
        }
    }
    catch (...)
    {
        Log::Error("An exception was raised during enumeration");
        return E_FAIL;
    }
    return S_OK;
}
