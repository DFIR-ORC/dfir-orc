//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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
    None = 0,
    TableFile = 1 << 0,
    StructuredFile = 1 << 1,
    File = 1 << 2,
    Archive = 1 << 3,
    Directory = 1 << 4,
    Pipe = 1 << 5,
    CSV = 1 << 6,
    TSV = 1 << 7,
    Parquet = 1 << 8,
    XML = 1 << 9,
    JSON = 1 << 10,
    ORC = 1 << 11
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
