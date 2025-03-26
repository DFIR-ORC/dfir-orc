//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "OutputSpec.h"

#include "Text/Fmt/OutputSpecTypes.h"

#include <boost/algorithm/string/join.hpp>

namespace Orc {
namespace Text {

void Print(Tree& node, const OutputSpec::Upload& upload)
{
    const auto serverInfo = fmt::format(L"{} ({})", upload.ServerName, upload.RootPath);
    PrintValue(node, L"Server", serverInfo);

    PrintValue(node, L"Method", upload.Method);
    PrintValue(node, L"Operation", upload.Operation);
    PrintValue(node, L"Mode", upload.Mode);
    PrintValue(node, L"User", upload.UserName.empty() ? kEmptyW : upload.UserName);
    PrintValue(node, L"Password", upload.Password.empty() ? L"<no>" : L"<yes>");
    PrintValue(node, L"Auth", upload.AuthScheme);
    PrintValue(node, L"Job", upload.JobName.empty() ? kEmptyW : upload.JobName);

    const auto includes = boost::join(upload.FilterInclude, L", ");
    PrintValue(node, L"Include", includes.empty() ? kEmptyW : includes);

    const auto excludes = boost::join(upload.FilterExclude, L", ");
    PrintValue(node, L"Exclude", excludes.empty() ? kEmptyW : excludes);
}

void Print(Tree& node, const OutputSpec& output)
{
    if (output.Path.empty())
    {
        Print(node, kEmpty);
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

    auto outputPath = output.Path;
    if (properties.size())
    {
        fmt::format_to(std::back_inserter(outputPath), L" ({})", fmt::join(properties, L", "));
    }

    Print(node, outputPath);

    if (output.UploadOutput != nullptr)
    {
        auto uploadNode = node.AddNode(4, L"Upload configuration:");
        Print(uploadNode, *(output.UploadOutput));
    }
}

}  // namespace Text
}  // namespace Orc
