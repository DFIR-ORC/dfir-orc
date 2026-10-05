//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <SetupAPI.h>
#include <winioctl.h>

#include <cstdint>
#include <string>

#include "Utils/BufferSpan.h"

#pragma managed(push, off)

namespace Orc {

class EnumDisk
{
public:
    class PhysicalDisk
    {
    public:
        PhysicalDisk() = default;
        PhysicalDisk(PhysicalDisk&& other) noexcept = default;
        std::wstring InterfacePath;

        // Identity as the storage stack reports it, each one empty when the device did not answer for it
        std::wstring VendorId;
        std::wstring ProductId;
        std::wstring ProductRevision;
        std::wstring SerialNumber;
        STORAGE_BUS_TYPE BusType = BusTypeUnknown;
        bool RemovableMedia = false;

        // Identity of the drive inside an enclosure, when the bridge answered SAT. Empty otherwise, and never a
        // replacement for the fields above: those stay as the bridge reported them.
        std::wstring EnclosedModel;
        std::wstring EnclosedSerial;
    };

public:
    HRESULT EnumerateDisks(std::vector<PhysicalDisk>& disks, const GUID guidDeviceClass = GUID_DEVINTERFACE_DISK);

    // The printable ASCII string at 'offset' in the bytes a StorageDeviceProperty query returned, trailing spaces
    // trimmed. A device controls both the offsets and the bytes, so this is empty for an absent (0) offset, for an
    // offset outside the buffer and for a string the storage stack did not terminate inside the buffer, and it
    // stops at the first byte that is not printable ASCII.
    static std::wstring ParseDescriptorString(BasicBufferSpan<const uint8_t> descriptor, DWORD offset);

    // 'wordCount' words from word 'firstWord' of an ATA IDENTIFY DEVICE page as a string. ATA holds the earlier
    // character in the high byte of each word, pads with spaces and does not terminate, so every word is byte
    // swapped and the trailing spaces are trimmed. Empty when the range is not inside the page; stops at the first
    // byte that is not printable ASCII.
    static std::wstring
    ParseAtaIdentifyString(BasicBufferSpan<const uint8_t> identify, size_t firstWord, size_t wordCount);

    ~EnumDisk();

private:
    HRESULT GetDevice(HDEVINFO hDevInfo, DWORD Index, PhysicalDisk& aDisk, const GUID guidDeviceClass);
};

}  // namespace Orc

#pragma managed(pop)
