//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "CompleteVolumeReader.h"

#pragma managed(push, off)

namespace Orc {

class VHDVolumeReader : public CompleteVolumeReader
{
public:
    typedef enum _DiskType
        : DWORD
    {
        None = 0,
        Reserved1 = 1,
        FixedHardDisk = 2,
        DynamicHardDisk = 3,
        DikfferencingHardDisk = 4,
        Reserved5 = 5,
        Reserved6 = 6
    } DiskType;

    typedef struct _Footer
    {
        CHAR Cookie[8];
        DWORD Features;
        DWORD Version;
        ULONGLONG DataOffset;
        DWORD TimeStamp;
        CHAR CreatorApplication;
        DWORD CreatorVersion;
        CHAR CreatorHostOS[4];
        ULONGLONG OriginalSize;
        ULONGLONG CurrentSize;
        struct _Geometry
        {
            SHORT Cylinders;
            BYTE Heads;
            BYTE SectorsPerTrack;
        } Geometry;
        DiskType DiskType;
        DWORD Checksum;
        UUID UniqueId;
        BYTE SavedState;
        BYTE Reserved[427];
    } Footer;

protected:
    Footer m_Footer;
    HRESULT LoadDiskFooter(void);

public:
    VHDVolumeReader(const WCHAR* szLocation);

    void Accept(VolumeReaderVisitor& visitor) const override { return visitor.Visit(*this); }

    const WCHAR* ShortVolumeName() { return L"\\"; }
    virtual HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }

    virtual HRESULT LoadDiskProperties();

    DiskType GetDiskType();

    std::shared_ptr<VolumeReader> GetVolumeReader();

    ~VHDVolumeReader(void);
};

class FixedVHDVolumeReader : public VHDVolumeReader
{

protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

public:
    FixedVHDVolumeReader(const WCHAR* szLocation)
        : VHDVolumeReader(szLocation)
    {
    }

    virtual HRESULT LoadDiskProperties();

    virtual std::shared_ptr<VolumeReader> DuplicateReader(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags);
};

}  // namespace Orc

#pragma managed(pop)
