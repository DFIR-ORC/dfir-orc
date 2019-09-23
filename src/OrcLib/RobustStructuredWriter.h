//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OutputSpec.h"

#include "StructuredOutputWriter.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API RobustStructuredWriter : public StructuredOutputWriter
{
protected:
    logger _L_;
    const std::shared_ptr<StructuredOutputWriter> m_pWriter;

public:
    RobustStructuredWriter(logger pLog, const std::shared_ptr<StructuredOutputWriter>& pWriter)
        : _L_(std::move(pLog))
        , m_pWriter(pWriter) {};

    HRESULT BeginElement(LPCWSTR szElement);
    HRESULT EndElement(LPCWSTR szElement);

    HRESULT BeginCollection(LPCWSTR szCollection);
    HRESULT EndCollection(LPCWSTR szCollection);

    HRESULT WriteNameValuePair(LPCWSTR szName, LPCWSTR szValue);
    HRESULT WriteNameFormatedStringPair(LPCWSTR szName, const WCHAR* szFormat, ...);

    HRESULT WriteNameValuePair(LPCWSTR szName, DWORD dwValue, bool bInHex = false);
    HRESULT WriteNameValuePair(LPCWSTR szName, ULONGLONG ullValue, bool bInHex = false);
    HRESULT WriteNameValuePair(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex = false);
    HRESULT WriteNameSizePair(LPCWSTR szName, size_t ullValue, bool bInHex = false);

    HRESULT WriteNameAttributesPair(LPCWSTR szName, DWORD dwFileAttibutes);

    HRESULT WriteNameFileTimePair(LPCWSTR szName, FILETIME fileTime);
    HRESULT WriteNameFileTimePair(LPCWSTR szName, LONGLONG fileTime);

    HRESULT WriteNameCharArrayPair(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount);
    HRESULT WriteNameCharArrayPair(LPCWSTR szName, const CHAR* szArray, DWORD dwCharCount);

    HRESULT WriteNameBytesInHexPair(LPCWSTR szName, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix);
    HRESULT WriteNameBytesInHexPair(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix);

    HRESULT WriteNameBoolPair(LPCWSTR szName, bool bBoolean);

    HRESULT WriteNameEnumPair(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[]);
    HRESULT WriteNameFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator);
    HRESULT WriteNameExactFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[]);

    HRESULT WriteNameIPPair(LPCWSTR szName, IN_ADDR& ip);
    HRESULT WriteNameIPPair(LPCWSTR szName, IN6_ADDR& ip);

    HRESULT WriteNameGUIDPair(LPCWSTR szName, const GUID& guid);

    HRESULT WriteComment(LPCWSTR szComment);

    HRESULT WriteString(LPCWSTR szString);

    HRESULT Close();

    virtual ~RobustStructuredWriter();
};

}  // namespace Orc
#pragma managed(pop)
