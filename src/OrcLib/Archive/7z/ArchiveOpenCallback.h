//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <system_error>

#include <7zip/extras.h>

namespace Orc {

namespace Archive {

class LogFileWriter;

class ArchiveOpenCallback
    : public IArchiveOpenCallback
    , public ICryptoGetTextPassword
    , public CMyUnknownImp
{
public:
    MY_UNKNOWN_IMP2(IArchiveOpenCallback, ICryptoGetTextPassword)

public:
    // IArchiveOpenCallback
    STDMETHOD(SetTotal)(const UInt64* files, const UInt64* bytes);
    STDMETHOD(SetCompleted)(const UInt64* files, const UInt64* bytes);

    // ICryptoGetTextPassword
    STDMETHOD(CryptoGetTextPassword)(BSTR* password);
};

}  // namespace Archive

}  // namespace Orc
