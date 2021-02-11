//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#include "stdafx.h"

#include "OutputSpecTypes.h"

namespace Orc {

std::wstring ToString(OutputSpecTypes::UploadAuthScheme scheme)
{
    switch (scheme)
    {
        case Orc::OutputSpecTypes::UploadAuthScheme::Anonymous:
            return L"Anonymous";
        case Orc::OutputSpecTypes::UploadAuthScheme::Basic:
            return L"Basic";
        case Orc::OutputSpecTypes::UploadAuthScheme::Kerberos:
            return L"Kerberos";
        case Orc::OutputSpecTypes::UploadAuthScheme::Negotiate:
            return L"Negotiate";
        case Orc::OutputSpecTypes::UploadAuthScheme::NTLM:
            return L"NTLM";
    }

    return L"Unknown";
}

std::wstring ToString(OutputSpecTypes::UploadMethod method)
{
    switch (method)
    {
        case Orc::OutputSpecTypes::UploadMethod::BITS:
            return L"Background Intelligent Transfer Service (BITS)";
        case Orc::OutputSpecTypes::UploadMethod::FileCopy:
            return L"File copy";
        case Orc::OutputSpecTypes::UploadMethod::NoUpload:
            return L"No upload";
    }

    return L"Unknown";
}

std::wstring ToString(OutputSpecTypes::UploadOperation operation)
{
    switch (operation)
    {
        case Orc::OutputSpecTypes::UploadOperation::Copy:
            return L"Copy file";
        case Orc::OutputSpecTypes::UploadOperation::Move:
            return L"Move file";
        case Orc::OutputSpecTypes::UploadOperation::NoOp:
            return L"No operation";
    }

    return L"Unknown";
}

std::wstring ToString(OutputSpecTypes::UploadMode mode)
{
    switch (mode)
    {
        case Orc::OutputSpecTypes::UploadMode::Asynchronous:
            return L"Asynchronous";
        case Orc::OutputSpecTypes::UploadMode::Synchronous:
            return L"Synchronous";
    }

    return L"Unknown";
}

std::wstring ToString(OutputSpecTypes::Kind kind)
{
    switch (kind)
    {
        case Orc::OutputSpecTypes::Kind::Archive:
            return L"archive";
        case Orc::OutputSpecTypes::Kind::CSV:
            return L"csv";
        case Orc::OutputSpecTypes::Kind::Directory:
            return L"directory";
        case Orc::OutputSpecTypes::Kind::File:
            return L"file";
        case Orc::OutputSpecTypes::Kind::JSON:
            return L"json";
        case Orc::OutputSpecTypes::Kind::None:
            return L"none";
        case Orc::OutputSpecTypes::Kind::ORC:
            return L"ORC";
        case Orc::OutputSpecTypes::Kind::Parquet:
            return L"parquet";
        case Orc::OutputSpecTypes::Kind::Pipe:
            return L"pipe";
        case Orc::OutputSpecTypes::Kind::StructuredFile:
            return L"structured file";
        case Orc::OutputSpecTypes::Kind::TableFile:
            return L"table file";
        case Orc::OutputSpecTypes::Kind::TSV:
            return L"tsv";
        case Orc::OutputSpecTypes::Kind::XML:
            return L"xml";
    }

    return L"Unknown";
}

std::wstring ToString(OutputSpecTypes::Encoding encoding)
{
    switch (encoding)
    {
        case Orc::OutputSpecTypes::Encoding::UTF8:
            return L"utf-8";
        case Orc::OutputSpecTypes::Encoding::UTF16:
            return L"utf-16";
    }

    return L"Unknown";
}

}  // namespace Orc
