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
    Z7_IFACES_IMP_UNK_2(IArchiveOpenCallback, ICryptoGetTextPassword)
};

}  // namespace Archive

}  // namespace Orc
