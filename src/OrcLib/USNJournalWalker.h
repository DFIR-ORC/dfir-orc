//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "IUSNJournalWalker.h"
#include "USNJournalWalkerBase.h"

#pragma managed(push, off)

namespace Orc {
class MountedVolumeReader;

class USNJournalWalker
    : public USNJournalWalkerBase
    , public IUSNJournalWalker
{
    DWORD m_dwVolumeSerialNumber;

public:
    USNJournalWalker();
    virtual ~USNJournalWalker();

    // from IUSNJournalWalker
    virtual HRESULT Initialize(const std::shared_ptr<Location>& loc);
    virtual HRESULT EnumJournal(const IUSNJournalWalker::Callbacks& pCallbacks);
    virtual HRESULT ReadJournal(const IUSNJournalWalker::Callbacks& pCallbacks);

private:
    HRESULT WalkRecords(const Callbacks& pCallbacks, bool bWalkDirs = true);
};  // USNJournalWalker
}  // namespace Orc

#pragma managed(pop)
