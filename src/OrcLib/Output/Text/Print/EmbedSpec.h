//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Print.h"

#include <EmbeddedResource.h>

namespace Orc {
namespace Text {

template <typename T>
void Print(Orc::Text::Tree<T>& node, const EmbeddedResource::EmbedSpec& embedItem)
{
    switch (embedItem.Type)
    {
        case EmbeddedResource::EmbedSpec::File:
            node.Add(L"File: {} {}", embedItem.Name, embedItem.Value);
            break;
        case EmbeddedResource::EmbedSpec::NameValuePair:
            node.Add(L"Value: {}={}", embedItem.Name, embedItem.Value);
            break;
        case EmbeddedResource::EmbedSpec::Archive: {
            for (const auto& archive : embedItem.ArchiveItems)
            {
                node.Add(L"Archive: {} -> {}", archive.Path, archive.Name);
            }
            break;
        }
        case EmbeddedResource::EmbedSpec::ValuesDeletion:
        case EmbeddedResource::EmbedSpec::BinaryDeletion:
            node.Add(L"Remove ID: {}", embedItem.Name);
            break;
    }
}

}  // namespace Text
}  // namespace Orc
