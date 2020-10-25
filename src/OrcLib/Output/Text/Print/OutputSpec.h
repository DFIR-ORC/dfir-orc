//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <boost/algorithm/string/join.hpp>

#include "Output/Text/Fmt/OutputSpecTypes.h"
#include "Output/Text/Print.h"
#include "Output/Text/Print/Bool.h"
#include "Output/Text/Print/Tribool.h"

#include <OutputSpec.h>

namespace Orc {
namespace Text {

template <typename T, typename U>
void PrintValue(Orc::Text::Tree<T>& node, const U& name, const OutputSpec::Upload& upload)
{
    auto uploadNode = node.AddNode(name);

    const auto serverInfo = fmt::format(L"{} ({})", upload.ServerName, upload.RootPath);
    PrintValue(uploadNode, "Server", serverInfo);

    PrintValue(uploadNode, "Method", upload.Method);
    PrintValue(uploadNode, "Operation", upload.Operation);
    PrintValue(uploadNode, "Mode", upload.Mode);
    PrintValue(uploadNode, "User", upload.UserName.empty() ? kStringEmptyW : upload.UserName);
    PrintValue(uploadNode, "Password", upload.Password.empty() ? L"<no>" : L"<yes>");
    PrintValue(uploadNode, "Auth", upload.AuthScheme);
    PrintValue(uploadNode, "Job", upload.JobName.empty() ? kStringEmptyW : upload.JobName);

    const auto includes = boost::join(upload.FilterInclude, L", ");
    PrintValue(uploadNode, "Include", includes.empty() ? kStringEmptyW : includes);

    const auto excludes = boost::join(upload.FilterExclude, L", ");
    PrintValue(uploadNode, "Exclude", excludes.empty() ? kStringEmptyW : excludes);
}

template <typename T, typename U>
void PrintValue(Orc::Text::Tree<T>& node, const U& name, const OutputSpec& output)
{
    if (output.Path.empty() && output.Type != OutputSpec::Kind::SQL)
    {
        PrintValue(node, fmt::format(L"{}", name), kStringEmpty);
        return;
    }

    std::vector<std::wstring> properties {ToString(output.Type)};

    if (output.Type != OutputSpec::Kind::None)
    {
        properties.push_back(ToString(output.OutputEncoding));
    }

    if (output.Type == OutputSpec::Kind::Archive)
    {
        // This parameter would be filled by the user
        if (output.Compression.size())
        {
            properties.push_back(output.Compression);
        }
    }

    if (output.Type == OutputSpec::Kind::SQL)
    {
        properties.push_back(output.ConnectionString);
    }

    auto outputPath = output.Path;
    if (properties.size())
    {
        fmt::format_to(std::back_inserter(outputPath), L" ({})", boost::join(properties, L", "));
    }

    PrintValue(node, name, outputPath);

    if (output.UploadOutput != nullptr)
    {
        PrintValue(node, L"Upload configuration:", *(output.UploadOutput));
    }
}

template <typename T, typename U>
void PrintValue(Orc::Text::Tree<T>& node, const U& name, const std::optional<OutputSpec>& output)
{
    if (output.has_value())
    {
        PrintValue(node, name, *output);
    }
}

}  // namespace Text
}  // namespace Orc
