//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "Location.h"

#include <memory>

#pragma managed(push, off)

namespace Orc {

class LocationOutput
{
public:
    std::shared_ptr<Location> m_pLoc;

    LocationOutput(const std::shared_ptr<Location>& loc)
        : m_pLoc(loc) {};
    LocationOutput(LocationOutput&& other) { std::swap(m_pLoc, other.m_pLoc); };

    const std::wstring& GetIdentifier() const
    {
        static const std::wstring empty;
        if (m_pLoc)
            return m_pLoc->GetIdentifier();
        else
            return empty;
    }
};
}  // namespace Orc

#pragma managed(pop)
