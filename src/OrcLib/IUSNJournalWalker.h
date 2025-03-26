//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>
#include <functional>

#pragma managed(push, off)

namespace Orc {

class Location;
class VolumeReader;

class IUSNJournalWalker
{

public:
    typedef std::function<void(std::shared_ptr<VolumeReader>& volreader, WCHAR* szFullName, USN_RECORD* pElt)>
        USNRecordCall;
    typedef std::function<void(const ULONG dwProgress)> ProgressCall;

    class Callbacks
    {
    public:
        USNRecordCall RecordCallback;
        ProgressCall ProgressCallback;
        Callbacks()
            : RecordCallback(nullptr)
            , ProgressCallback(nullptr) {};
    };

    IUSNJournalWalker() {}
    virtual ~IUSNJournalWalker(void) {}

    virtual HRESULT Initialize(const std::shared_ptr<Location>& loc) = 0;
    virtual HRESULT EnumJournal(const Callbacks& pCallbacks) = 0;
    virtual HRESULT ReadJournal(const Callbacks& pCallbacks) = 0;
};  // IUSNJournalWalker
}  // namespace Orc

#pragma managed(pop)
