//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "Partition.h"

#include <sstream>

#include <boost/io/ios_state.hpp>

using namespace Orc;

std::wostream& Orc::operator<<(std::wostream& o, const Orc::Partition& p)
{
    boost::io::ios_flags_saver fs(std::cerr);

    if (p.IsNTFS())
        o << L"NTFS";
    else if (p.IsFAT12())
        o << L"FAT12";
    else if (p.IsFAT16())
        o << L"FAT16";
    else if (p.IsFAT32())
        o << L"FAT32";
    else if (p.IsREFS())
        o << L"REFS";
    else if (p.IsESP())
        o << L"ESP";
    else if (p.IsMicrosoftReserved())
        o << L"MICROSOFT_RESERVED";
    else if (p.IsBitLocked())
        o << L"BitLocked";
    else if (!p.IsValid())
        o << L"INVALID";
    else
        o << L"OTHER";

    if (p.IsValid())
    {
        o << L" - number : " << p.PartitionNumber << L" - start offset : 0x" << std::hex << p.Start
          << L" - end offset : 0x" << p.End << L" - size : 0x" << p.Size << L" - flags :";

        std::wstringstream flags;

        if (p.IsBootable())
        {
            flags << L" BOOTABLE";
        }
        if (p.IsSystem())
        {
            flags << L" SYSTEM";
        }
        if (p.IsReadOnly())
        {
            flags << L" RO";
        }
        if (p.IsHidden())
        {
            flags << L" HIDDEN";
        }
        if (p.IsNotAutoMountable())
        {
            flags << L" NO_AUTO_MOUNT";
        }

        if (flags.str().empty())
        {
            o << L" NONE";
        }
        else
        {
            o << flags.str();
        }
    }

    return o;
}
