//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "VolumeShadowCopies.h"

#include "Location.h"

#include "SystemDetails.h"

#include "VssAPIExtension.h"

#include "OutputWriter.h"

#pragma warning(disable : 4091)

#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>

#pragma warning(default : 4091)

using namespace Orc;

const Orc::FlagsDefinition VolumeShadowCopies::g_VssFlags[] = {
    {0x00000001, L"VSS_VOLSNAP_ATTR_PERSISTENT", L"The shadow copy is persistent across reboots."},
    {0x00000002, L"VSS_VOLSNAP_ATTR_NO_AUTORECOVERY", L"Auto-recovery is disabled for the shadow copy."},
    {0x00000004,
     L"VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE",
     L"The specified shadow copy is a client-accessible shadow copy that supports Shadow Copies for Shared Folders and "
     L"should not be exposed."},
    {0x00000008,
     L"VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE",
     L"The shadow copy is not automatically deleted when the shadow copy requester process ends."},
    {0x00000010, L"VSS_VOLSNAP_ATTR_NO_WRITERS", L"No writers are involved in creating the shadow copy."},
    {0x00000020,
     L"VSS_VOLSNAP_ATTR_TRANSPORTABLE",
     L"The shadow copy is to be transported and therefore should not be surfaced locally."},
    {0x00000040, L"VSS_VOLSNAP_ATTR_NOT_SURFACED", L"The shadow copy is not currently exposed."},
    {0x00000080, L"VSS_VOLSNAP_ATTR_NOT_TRANSACTED", L"The shadow copy is not transacted."},
    {0x00010000, L"VSS_VOLSNAP_ATTR_HARDWARE_ASSISTED", L"Indicates that a given provider is a hardware provider."},
    {0x00020000,
     L"VSS_VOLSNAP_ATTR_DIFFERENTIAL",
     L"Indicates that a given provider uses differential data or a copy-on-write mechanism to implement shadow "
     L"copies."},
    {0x00040000,
     L"VSS_VOLSNAP_ATTR_PLEX",
     L"Indicates that a given provider uses a PLEX or mirrored split mechanism to implement shadow copies."},
    {0x00080000, L"VSS_VOLSNAP_ATTR_IMPORTED", L"The shadow copy of the volume was imported onto this machine."},
    {0x00100000, L"VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY", L"The shadow copy is locally exposed."},
    {0x00200000, L"VSS_VOLSNAP_ATTR_EXPOSED_REMOTELY", L"The shadow copy is remotely exposed."},
    {0x00400000,
     L"VSS_VOLSNAP_ATTR_AUTORECOVER",
     L"Indicates that the writer will need to auto-recover the component."},
    {0x00800000,
     L"VSS_VOLSNAP_ATTR_ROLLBACK_RECOVERY",
     L"Indicates that the writer will need to auto-recover the component."},
    {0x01000000, L"VSS_VOLSNAP_ATTR_DELAYED_POSTSNAPSHOT", L"Reserved for system use."},
    {0x00000200,
     L"VSS_VOLSNAP_ATTR_TXF_RECOVERY",
     L"Indicates that TxF recovery should be enforced during shadow copy creation."},
    {0xFFFFFFFF, NULL, NULL}};

HRESULT VolumeShadowCopies::EnumerateShadows(std::vector<Shadow>& shadows)
{
    HRESULT hr = E_FAIL;

    DWORD dwMajor = 0L, dwMinor = 0L;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    if (dwMajor == 5 && dwMinor < 3)
    {
        // Seems VSSAPI.dll won't load on XP....
        return S_OK;
    }

    try
    {
        m_vssapi = ExtensionLibrary::GetLibrary<VssAPIExtension>();
        if (m_vssapi == nullptr)
        {
            Log::Error("Failed to load VSSAPI dll, feature is not available");
            return E_FAIL;
        }

        CComPtr<IVssBackupComponents> pVssBackup = NULL;
        if (FAILED(hr = m_vssapi->CreateVssBackupComponents(&pVssBackup)))
        {
            if (hr == E_ACCESSDENIED)
            {
                Log::Warn("Access denied when connecting to VSS Service (not running as admin?) (code: {:#x})", hr);
                return hr;
            }
            else
            {
                Log::Error("Failed to connect to VSS service (code: {:#x})", hr);
                return hr;
            }
        }

        if (FAILED(hr = pVssBackup->InitializeForBackup()))
        {
            if ((hr == VSS_E_UNEXPECTED) && SystemDetails::IsWOW64())
            {
                Log::Warn(
                    "Failed to initalise VSS service, most likely cause: you are running a 32 bits process on x64 "
                    "system (code: {:#x})",
                    hr);
            }
            else
            {
                Log::Error("Failed to initalise VSS service (code: {:#x})", hr);
            }
            return hr;
        }

        if (FAILED(hr = pVssBackup->SetContext(VSS_CTX_ALL)))
        {
            Log::Error("Failed to set context on VSS service (code: {:#x})", hr);
            return hr;
        }

        CComPtr<IVssEnumObject> pEnum;

        hr = pVssBackup->Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pEnum);

        const ULONG ulSnapCount = 3;
        VSS_OBJECT_PROP Snaps[ulSnapCount];

        ZeroMemory((BYTE*)Snaps, sizeof(VSS_OBJECT_PROP) + ulSnapCount);

        ULONG ulSnapReturned = 0;

        while (SUCCEEDED(pEnum->Next(ulSnapCount, Snaps, &ulSnapReturned)))
        {

            for (ULONG i = 0; i < ulSnapReturned; i++)
            {
                if (Snaps[i].Type == VSS_OBJECT_SNAPSHOT)
                {
                    shadows.emplace_back(
                        Snaps[i].Obj.Snap.m_pwszOriginalVolumeName,
                        Snaps[i].Obj.Snap.m_pwszSnapshotDeviceObject,
                        (VSS_VOLUME_SNAPSHOT_ATTRIBUTES)Snaps[i].Obj.Snap.m_lSnapshotAttributes,
                        Snaps[i].Obj.Snap.m_tsCreationTimestamp,
                        Snaps[i].Obj.Snap.m_SnapshotId);

                    m_vssapi->VssFreeSnapshotProperties(&Snaps[i].Obj.Snap);
                }
                else
                {
                    Log::Warn("Unexpected object type '{}' returned by the IVssEnumObject", Snaps[i].Type);
                }
            }
            if (ulSnapReturned < ulSnapCount)
                break;

            ZeroMemory((BYTE*)Snaps, sizeof(VSS_OBJECT_PROP) + ulSnapCount);
        }

        pEnum.Release();
        pVssBackup.Release();
    }
    catch (const std::exception& e)
    {
        Log::Error("System Exception during snapshot enumeration: {}", e.what());
        return E_FAIL;
    }
    return S_OK;
}

VolumeShadowCopies::~VolumeShadowCopies() {}
