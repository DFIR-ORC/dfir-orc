//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ArchiveOpenCallback
    : public IArchiveOpenCallback
    , public ICryptoGetTextPassword
{
private:
    long m_refCount;

    logger _L_;

public:
    ArchiveOpenCallback(logger pLog);
    virtual ~ArchiveOpenCallback();

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IArchiveOpenCallback
    STDMETHOD(SetTotal)(const UInt64* files, const UInt64* bytes);
    STDMETHOD(SetCompleted)(const UInt64* files, const UInt64* bytes);

    // ICryptoGetTextPassword
    STDMETHOD(CryptoGetTextPassword)(BSTR* password);
};

}  // namespace Orc

#pragma managed(pop)
