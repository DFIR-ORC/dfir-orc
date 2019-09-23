//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OutputWriter.h"

#include "SystemDetails.h"

#include <regex>

using namespace std;

using namespace Orc;

HRESULT
OutputWriter::GetOutputFileName(const std::wstring& strPattern, const std::wstring& strName, std::wstring& strFileName)
{
    static const std::wregex r_Name(L"\\{Name\\}");
    static const std::wregex r_FileName(L"\\{FileName\\}");
    static const std::wregex r_DirectoryName(L"\\{DirectoryName\\}");
    static const std::wregex r_ComputerName(L"\\{ComputerName\\}");
    static const std::wregex r_FullComputerName(L"\\{FullComputerName\\}");
    static const std::wregex r_TimeStamp(L"\\{TimeStamp\\}");
    static const std::wregex r_SystemType(L"\\{SystemType\\}");

    auto s0 = strPattern;

    wstring fmt_keyword = strName;
    auto s1 = std::regex_replace(s0, r_Name, fmt_keyword);

    wstring fmt_name = strName;
    auto s2 = std::regex_replace(s1, r_FileName, fmt_name);
    auto s3 = std::regex_replace(s2, r_DirectoryName, fmt_name);

    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);
    auto s4 = std::regex_replace(s3, r_ComputerName, strComputerName);

    wstring strFullComputerName;
    SystemDetails::GetOrcFullComputerName(strFullComputerName);
    auto s5 = std::regex_replace(s4, r_FullComputerName, strFullComputerName);

    wstring strTimeStamp;
    SystemDetails::GetTimeStamp(strTimeStamp);
    auto s6 = std::regex_replace(s5, r_TimeStamp, strTimeStamp);

    wstring strSystemType;
    SystemDetails::GetSystemType(strSystemType);
    auto s7 = std::regex_replace(s6, r_SystemType, strSystemType);

    std::swap(s7, strFileName);
    return S_OK;
}
