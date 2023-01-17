//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <strsafe.h>

#include <boost/scope_exit.hpp>

#include "USNInfo.h"
#include "USNJournalWalker.h"
#include "USNJournalWalkerOffline.h"
#include "USNRecordFileInfo.h"
#include "TableOutputWriter.h"
#include "SnapshotVolumeReader.h"
#include "SystemDetails.h"
#include "FileStream.h"
#include "PipeStream.h"

using namespace Orc;
using namespace Orc::Command::USNInfo;

static const FlagsDefinition g_Reasons[] = {
    {0x00008000,
     L"BASIC_INFO_CHANGE",
     L"A user has either changed one or more file or directory attributes (for example, the read-only, hidden, system, "
     L"archive, or sparse attribute), or one or more time stamps."},
    {0x80000000, L"CLOSE", L"The file or directory is closed."},
    {0x00020000,
     L"COMPRESSION_CHANGE",
     L"The compression state of the file or directory is changed from or to compressed."},
    {0x00000002, L"DATA_EXTEND", L"The file or directory is extended (added to)."},
    {0x00000001, L"DATA_OVERWRITE", L"The data in the file or directory is overwritten."},
    {0x00000004, L"DATA_TRUNCATION", L"The file or directory is truncated."},
    {0x00000400,
     L"EA_CHANGE",
     L"The user made a change to the extended attributes of a file or directory. These NTFS file system attributes are "
     L"not accessible to Windows-based applications."},
    {0x00040000, L"ENCRYPTION_CHANGE", L"The file or directory is encrypted or decrypted."},
    {0x00000100, L"FILE_CREATE", L"The file or directory is created for the first time."},
    {0x00000200, L"FILE_DELETE", L"The file or directory is deleted."},
    {0x00010000,
     L"HARD_LINK_CHANGE",
     L"An NTFS file system hard link is added to or removed from the file or directory. An NTFS file system hard link, "
     L"similar to a POSIX hard link, is one of several directory entries that see the same file or directory."},
    {0x00004000,
     L"INDEXABLE_CHANGE",
     L"A user changes the FILE_ATTRIBUTE_NOT_CONTENT_INDEXED attribute. That is, the user changes the file or "
     L"directory from one where content can be indexed to one where content cannot be indexed, or vice versa. Content "
     L"indexing permits rapid searching of data by building a database of selected content."},
    {0x00000020, L"NAMED_DATA_EXTEND", L"The one or more named data streams for a file are extended (added to)."},
    {0x00000010, L"NAMED_DATA_OVERWRITE", L"The data in one or more named data streams for a file is overwritten."},
    {0x00000040, L"NAMED_DATA_TRUNCATION", L"The one or more named data streams for a file is truncated."},
    {0x00080000, L"OBJECT_ID_CHANGE", L"The object identifier of a file or directory is changed."},
    {0x00002000,
     L"RENAME_NEW_NAME",
     L"A file or directory is renamed, and the file name in the USN_RECORD structure is the new name."},
    {0x00001000,
     L"RENAME_OLD_NAME",
     L"The file or directory is renamed, and the file name in the USN_RECORD structure is the previous name."},
    {0x00100000,
     L"REPARSE_POINT_CHANGE",
     L"The reparse point that is contained in a file or directory is changed, or a reparse point is added to or "
     L"deleted from a file or directory."},
    {0x00000800, L"SECURITY_CHANGE", L"A change is made in the access rights to a file or directory."},
    {0x00200000, L"STREAM_CHANGE", L"A named stream is added to or removed from a file, or a named stream is renamed."},
    {0xFFFFFFFF, NULL, NULL}};

HRESULT Main::USNRecordInformation(
    ITableOutput& output,
    const std::shared_ptr<VolumeReader>& volreader,
    WCHAR* szFullName,
    USN_RECORD* pElt)
{
    try
    {
        HRESULT hr = S_OK;

        // ComputerName
        SystemDetails::WriteOrcComputerName(output);

        // USN
        output.WriteInteger(pElt->Usn);

        // FRN
        output.WriteInteger(pElt->FileReferenceNumber);

        // ParentFRN
        output.WriteInteger(pElt->ParentFileReferenceNumber);

        // TimeStamp
        FILETIME ft = {pElt->TimeStamp.u.LowPart, (DWORD)pElt->TimeStamp.u.HighPart};
        output.WriteFileTime(ft);

        // FileName
        output.WriteString(std::wstring_view(pElt->FileName, pElt->FileNameLength / sizeof(WCHAR)));

        if (!config.bCompactForm)
        {
            output.WriteString(szFullName);
        }
        else
        {
            output.WriteNothing();
        }

        output.WriteAttributes(pElt->FileAttributes);

        if (!config.bCompactForm)
        {
            // Nicely formatted reason
            output.WriteFlags(pElt->Reason);
        }
        else
        {
            output.WriteInteger(pElt->Reason);
        }

        output.WriteInteger(volreader->VolumeSerialNumber());

        auto snapshot_reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(volreader);

        if (snapshot_reader)
        {
            output.WriteGUID(snapshot_reader->GetSnapshotID());
        }
        else
        {
            output.WriteGUID(GUID_NULL);
        }

        output.WriteEndOfLine();
    }
    catch (WCHAR* e)
    {
        Log::Error(L"Exception: could not WriteFileInformation for '{}': {}", szFullName, e);
    }

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = LoadWinTrust();
    if (FAILED(hr))
    {
        return hr;
    }

    const auto& unique_locs = config.locs.GetAltitudeLocations();
    std::vector<std::shared_ptr<Location>> locations;

    // keep only the locations we're parsing
    std::copy_if(
        std::cbegin(unique_locs),
        std::cend(unique_locs),
        std::back_inserter(locations),
        [](const std::shared_ptr<Location>& item) -> bool { return item->GetParse(); });

    if (config.output.Type == OutputSpec::Kind::Archive)
    {
        hr = m_outputs.Prepare(config.output);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to prepare archive for '{}' [{}]", config.output.Path, SystemError(hr));
            return hr;
        }
    }

    BOOST_SCOPE_EXIT(&config, &m_outputs) { m_outputs.CloseAll(config.output); }
    BOOST_SCOPE_EXIT_END;

    hr = m_outputs.GetWriters(config.output, L"USNInfo", locations);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to get writers for locations [{}]", SystemError(hr));
        return hr;
    }

    hr = m_outputs.ForEachOutput(
        config.output, [this](const MultipleOutput<LocationOutput>::OutputPair& dir) -> HRESULT {
            m_console.Print(L"Parsing volume '{}'", dir.first.m_pLoc->GetLocation());
            USNJournalWalkerOffline walker;

            HRESULT hr = walker.Initialize(dir.first.m_pLoc);
            if (FAILED(hr))
            {
                if (hr == HRESULT_FROM_WIN32(ERROR_FILE_SYSTEM_LIMITATION))
                {
                    Log::Warn(L"File system not eligible for volume '{}'", dir.first.m_pLoc->GetLocation());
                    return S_OK;
                }

                Log::Critical(
                    L"Failed to init walk for volume '{}' [{}]", dir.first.m_pLoc->GetLocation(), SystemError(hr));
                return hr;
            }

            if (!walker.GetUsnJournal())
            {
                Log::Warn(L"Did not find a USN journal on following volume '{}'", dir.first.m_pLoc->GetLocation());
                return S_OK;
            }

            IUSNJournalWalker::Callbacks callbacks;
            callbacks.RecordCallback =
                [](const std::shared_ptr<VolumeReader>& volreader, WCHAR* szFullName, USN_RECORD* pElt) {};

            hr = walker.EnumJournal(callbacks);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to enum MFT records '{}' [{}]", dir.first.m_pLoc->GetLocation(), SystemError(hr));
                return S_OK;
            }

            callbacks.RecordCallback =
                [this, dir](const std::shared_ptr<VolumeReader>& volreader, WCHAR* szFullName, USN_RECORD* pElt) {
                    USNRecordInformation(*dir.second, volreader, szFullName, pElt);
                };

            hr = walker.ReadJournal(callbacks);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to walk volume '{}' [{}]", dir.first.m_pLoc->GetLocation(), SystemError(hr));
                return S_OK;
            }

            Log::Info(L"Done");
            return S_OK;
        });

    if (FAILED(hr))
    {
        Log::Error("Failed during the enumeration of output items [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}
