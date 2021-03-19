//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#pragma once

#include <string>

#include "Utils/EnumFlags.h"

namespace Orc {
namespace OutputSpecTypes {

enum class UploadAuthScheme : char
{
    Anonymous = 0,
    Basic,
    NTLM,
    Kerberos,
    Negotiate
};

enum class UploadMethod : char
{
    NoUpload = 0,
    FileCopy,
    BITS
};

enum BITSMode : char
{
    NoMode,
    SMB,
    HTTP,
    HTTPS
};

enum class UploadOperation : char
{
    NoOp = 0,
    Copy,
    Move
};

enum class UploadMode : char
{
    Synchronous = 0,
    Asynchronous
};

enum class Kind
{
    None = 1,
    TableFile = 1 << 1,
    StructuredFile = 1 << 2,
    File = 1 << 3,
    Archive = 1 << 4,
    Directory = 1 << 5,
    Pipe = 1 << 6,
    CSV = 1 << 7,
    TSV = 1 << 8,
    Parquet = 1 << 9,
    XML = 1 << 10,
    JSON = 1 << 11,
    ORC = 1 << 12
};

enum Disposition
{
    Truncate = 0,
    CreateNew,
    Append
};

enum Status
{
    StatusUnknown,
    Existing,
    CreatedNew
};

enum class Encoding
{
    kUnknown = 0,
    UTF8,
    UTF16
};

}  // namespace OutputSpecTypes

std::wstring ToString(OutputSpecTypes::UploadAuthScheme method);
std::wstring ToString(OutputSpecTypes::UploadMethod method);
std::wstring ToString(OutputSpecTypes::UploadOperation method);
std::wstring ToString(OutputSpecTypes::UploadMode mode);
std::wstring ToString(OutputSpecTypes::Kind kind);
std::wstring ToString(OutputSpecTypes::Encoding encoding);

ENABLE_BITMASK_OPERATORS(OutputSpecTypes::Kind);

}  // namespace Orc
