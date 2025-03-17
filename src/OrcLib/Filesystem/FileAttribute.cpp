//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2023 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "FileAttribute.h"

#include <array>
#include <tuple>

#include "Log/Log.h"
#include "Utils/String.h"

using namespace Orc;

namespace {

constexpr std::string_view kUnknown = "<unknown>";
constexpr std::string_view kNone = "<none>";

constexpr std::string_view kFileAttributeReadOnly = "FILE_ATTRIBUTE_READONLY";
constexpr std::string_view kFileAttributeHidden = "FILE_ATTRIBUTE_HIDDEN";
constexpr std::string_view kFileAttributeSystem = "FILE_ATTRIBUTE_SYSTEM";
constexpr std::string_view kFileAttributeDirectory = "FILE_ATTRIBUTE_DIRECTORY";
constexpr std::string_view kFileAttributeArchive = "FILE_ATTRIBUTE_ARCHIVE";
constexpr std::string_view kFileAttributeDevice = "FILE_ATTRIBUTE_DEVICE";
constexpr std::string_view kFileAttributeNormal = "FILE_ATTRIBUTE_NORMAL";
constexpr std::string_view kFileAttributeTemporary = "FILE_ATTRIBUTE_TEMPORARY";
constexpr std::string_view kFileAttributeSparseFile = "FILE_ATTRIBUTE_SPARSE_FILE";
constexpr std::string_view kFileAttributeReparsePoint = "FILE_ATTRIBUTE_REPARSE_POINT";
constexpr std::string_view kFileAttributeCompressed = "FILE_ATTRIBUTE_COMPRESSED";
constexpr std::string_view kFileAttributeOffline = "FILE_ATTRIBUTE_OFFLINE";
constexpr std::string_view kFileAttributeNotContentIndexed = "FILE_ATTRIBUTE_NOT_CONTENT_INDEXED";
constexpr std::string_view kFileAttributeEncrypted = "FILE_ATTRIBUTE_ENCRYPTED";
constexpr std::string_view kFileAttributeIntegrityStream = "FILE_ATTRIBUTE_INTEGRITY_STREAM";
constexpr std::string_view kFileAttributeVirtual = "FILE_ATTRIBUTE_VIRTUAL";
constexpr std::string_view kFileAttributeNoScrubData = "FILE_ATTRIBUTE_NO_SCRUB_DATA";
constexpr std::string_view kFileAttributeEA = "FILE_ATTRIBUTE_EA";
constexpr std::string_view kFileAttributePinned = "FILE_ATTRIBUTE_PINNED";
constexpr std::string_view kFileAttributeUnpinned = "FILE_ATTRIBUTE_UNPINNED";
constexpr std::string_view kFileAttributeRecallOnOpen = "FILE_ATTRIBUTE_RECALL_ON_OPEN";
constexpr std::string_view kFileAttributeRecallOnDataAccess = "FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS";
constexpr std::string_view kFileAttributeStrictlySequential = "FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL";
constexpr std::string_view kFileAttributeCascadesRemote = "FILE_ATTRIBUTE_CASCADES_REMOTE";

// Letters are from 'attrib' then 'propsys.dll' or guessed
constexpr std::array kFileAttributes = {
    std::tuple {FileAttribute::kFileAttributeReadOnly, kFileAttributeReadOnly, 'R'},
    std::tuple {FileAttribute::kFileAttributeHidden, kFileAttributeHidden, 'H'},
    std::tuple {FileAttribute::kFileAttributeSystem, kFileAttributeSystem, 'S'},
    std::tuple {FileAttribute::kFileAttributeDirectory, kFileAttributeDirectory, 'D'},  // propsys
    std::tuple {FileAttribute::kFileAttributeArchive, kFileAttributeArchive, 'A'},
    std::tuple {FileAttribute::kFileAttributeDevice, kFileAttributeDevice, 'd'},  // 'X' (propsys)
    std::tuple {FileAttribute::kFileAttributeNormal, kFileAttributeNormal, 'N'},  // propsys
    std::tuple {FileAttribute::kFileAttributeTemporary, kFileAttributeTemporary, 'T'},  // propsys
    std::tuple {FileAttribute::kFileAttributeSparseFile, kFileAttributeSparseFile, 'p'},  // 'P' (propsys)
    std::tuple {FileAttribute::kFileAttributeReparsePoint, kFileAttributeReparsePoint, 'L'},  // propsys
    std::tuple {FileAttribute::kFileAttributeCompressed, kFileAttributeCompressed, 'C'},  // propsys
    std::tuple {FileAttribute::kFileAttributeOffline, kFileAttributeOffline, 'O'},
    std::tuple {FileAttribute::kFileAttributeNotContentIndexed, kFileAttributeNotContentIndexed, 'I'},
    std::tuple {FileAttribute::kFileAttributeEncrypted, kFileAttributeEncrypted, 'E'},  // propsys
    std::tuple {FileAttribute::kFileAttributeIntegrityStream, kFileAttributeIntegrityStream, 'V'},
    std::tuple {FileAttribute::kFileAttributeVirtual, kFileAttributeVirtual, 'v'},  // 'V' (propsys)
    std::tuple {FileAttribute::kFileAttributeNoScrubData, kFileAttributeNoScrubData, 'X'},
    std::tuple {FileAttribute::kFileAttributeEA, kFileAttributeEA, 'e'},
    std::tuple {FileAttribute::kFileAttributePinned, kFileAttributePinned, 'P'},
    std::tuple {FileAttribute::kFileAttributeUnpinned, kFileAttributeUnpinned, 'U'},
    // std::tuple {FileAttribute::kFileAttributeRecallOnOpen, kFileAttributeRecallOnOpen, 'o'},
    std::tuple {FileAttribute::kFileAttributeRecallOnDataAccess, kFileAttributeRecallOnDataAccess, 'M'},  // propsys
    std::tuple {FileAttribute::kFileAttributeStrictlySequential, kFileAttributeStrictlySequential, 'B'},
    std::tuple {FileAttribute::kFileAttributeCascadesRemote, kFileAttributeCascadesRemote, 'c'}};

bool HasInvalidFlag(uint32_t flags)
{
    using namespace Orc;

    auto remainingFlags = flags;
    for (const auto& [flag, string, identifier] : kFileAttributes)
    {
        if (HasFlag(static_cast<FileAttribute>(flags), flag))
        {
            remainingFlags -= static_cast<std::underlying_type_t<FileAttribute>>(flag);
        }
    }

    if (remainingFlags)
    {
        return true;
    }

    return false;
}

}  // namespace

namespace Orc {

std::string ToString(FileAttribute flags)
{
    if (flags == static_cast<FileAttribute>(0))
    {
        return std::string(kNone);
    }

    if (::HasInvalidFlag(std::underlying_type_t<FileAttribute>(flags)))
    {
        return fmt::format("{:#x}", std::underlying_type_t<FileAttribute>(flags));
    }

    std::vector<std::string_view> strings;
    for (const auto& [flag, string, identifier] : kFileAttributes)
    {
        if (HasFlag(flags, flag))
        {
            strings.push_back(string);
        }
    }

    std::string output;
    Join(std::cbegin(strings), std::cend(strings), std::back_inserter(output), '|');
    return output;
}

std::wstring ToStringW(FileAttribute flags)
{
    auto s = ToString(flags);

    std::wstring output;
    std::copy(std::cbegin(s), std::cend(s), std::back_inserter(output));
    return output;
}

char ToIdentifier(FileAttribute flags)
{
    if (flags == static_cast<FileAttribute>(0))
    {
        return '?';
    }

    if (::HasInvalidFlag(std::underlying_type_t<FileAttribute>(flags)))
    {
        return '?';
    }

    for (const auto& [f, s, identifier] : kFileAttributes)
    {
        if (HasFlag(flags, f))
        {
            if (flags != f)
            {
                return '?';
            }

            return identifier;
        }
    }

    return '?';
}

wchar_t ToIdentifierW(FileAttribute flags)
{
    return ToIdentifier(flags);
}

std::string ToIdentifiers(FileAttribute flags)
{
    if (flags == static_cast<FileAttribute>(0))
    {
        return std::string(kNone);
    }

    if (::HasInvalidFlag(std::underlying_type_t<FileAttribute>(flags)))
    {
        return fmt::format("{:#x}", std::underlying_type_t<FileAttribute>(flags));
    }

    std::string output;
    for (const auto& [flag, s, identifier] : kFileAttributes)
    {
        if (HasFlag(flags, flag))
        {
            output.push_back(identifier);
        }
        else
        {
            output.push_back('.');
        }
    }

    return output;
}

std::wstring ToIdentifiersW(FileAttribute flags)
{
    auto s = ToIdentifiers(flags);

    std::wstring output;
    std::copy(std::cbegin(s), std::cend(s), std::back_inserter(output));
    return output;
}

FileAttribute ToFileAttribute(const uint32_t attributes, std::error_code& ec)
{
    if (::HasInvalidFlag(std::underlying_type_t<FileAttribute>(attributes)))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
    }

    return static_cast<FileAttribute>(attributes);
}

}  // namespace Orc
