//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#pragma once

#include "FileFind.h"

#include <memory>
#include <string>

#pragma managed(push, off)

namespace Orc {

class ByteStream;

class Hive
{
public:
    Hive()
        : Stream(nullptr) {};

    Hive(Hive&& anOther)
    {
        std::swap(Stream, anOther.Stream);
        std::swap(FileName, anOther.FileName);
        std::swap(Match, anOther.Match);
    }

    std::shared_ptr<ByteStream> Stream;
    std::wstring FileName;
    std::shared_ptr<FileFind::Match> Match;
};
}  // namespace Orc

#pragma managed(pop)
