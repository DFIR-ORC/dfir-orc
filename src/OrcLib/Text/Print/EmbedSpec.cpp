//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "EmbedSpec.h"

namespace Orc {
namespace Text {

void Print(Tree& node, const EmbeddedResource::EmbedSpec& embedItem)
{
    switch (embedItem.Type)
    {
        case EmbeddedResource::EmbedSpec::EmbedType::File:
            node.Add(L"File: {} {}", embedItem.Name, embedItem.Value);
            break;
        case EmbeddedResource::EmbedSpec::EmbedType::NameValuePair:
            node.Add(L"Value: {}={}", embedItem.Name, embedItem.Value);
            break;
        case EmbeddedResource::EmbedSpec::EmbedType::Archive: {
            for (const auto& archive : embedItem.ArchiveItems)
            {
                node.Add(L"Archive: {} -> {}", archive.Path, archive.Name);
            }
            break;
        }
        case EmbeddedResource::EmbedSpec::EmbedType::ValuesDeletion:
        case EmbeddedResource::EmbedSpec::EmbedType::BinaryDeletion:
            node.Add(L"Remove ID: {}", embedItem.Name);
            break;
    }
}

}  // namespace Text
}  // namespace Orc
