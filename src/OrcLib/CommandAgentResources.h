//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <map>
#include <boost/logic/tribool.hpp>

#include "OrcLib.h"

#include "CaseInsensitive.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API CommandAgentResources
{
private:
    std::wstring m_strTempDirectory;
    std::map<std::wstring, std::wstring, CaseInsensitive> m_TempRessources;

    HRESULT ExtractRessource(const std::wstring& strRef, const std::wstring& strKeyword, std::wstring& strExtracted);

public:
    CommandAgentResources() {}

    HRESULT SetTempDirectory(const std::wstring& strTempDir)
    {
        m_strTempDirectory = strTempDir;
        return S_OK;
    }

    boost::logic::tribool IsRessourceAvailable(const std::wstring& strResourceRef)
    {
        auto it = m_TempRessources.find(strResourceRef);
        if (it != end(m_TempRessources))
        {
            if (it->second.empty())
                return false;
            return true;
        }
        else
        {
            return boost::logic::indeterminate;
        }
    }

    HRESULT GetResource(const std::wstring& strResourceRef, const std::wstring& strKeyword, std::wstring& strResource);

    HRESULT DeleteTemporaryRessource(const std::wstring& strResourceRef);
    HRESULT DeleteTemporaryRessources();

    ~CommandAgentResources();
};
}  // namespace Orc

#pragma managed(pop)
