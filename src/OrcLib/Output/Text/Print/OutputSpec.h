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

template <>
struct Printer<OutputSpec::Upload>
{
    template <typename T>
    static void Output(Orc::Text::Tree<T>& root, const OutputSpec::Upload& upload)
    {
        const auto serverInfo = fmt::format(L"{} ({})", upload.ServerName, upload.RootPath);
        PrintValue(root, "Server", serverInfo);

        PrintValue(root, "Method", upload.Method);
        PrintValue(root, "Operation", upload.Operation);
        PrintValue(root, "Mode", upload.Mode);
        PrintValue(root, "User", upload.UserName.empty() ? kStringEmptyW : upload.UserName);
        PrintValue(root, "Password", upload.Password.empty() ? L"<no>" : L"<yes>");
        PrintValue(root, "Auth", upload.AuthScheme);
        PrintValue(root, "Job", upload.JobName.empty() ? kStringEmptyW : upload.JobName);

        const auto includes = boost::join(upload.FilterInclude, L", ");
        PrintValue(root, "Include", includes.empty() ? kStringEmptyW : includes);

        const auto excludes = boost::join(upload.FilterExclude, L", ");
        PrintValue(root, "Exclude", excludes.empty() ? kStringEmptyW : excludes);
    }
};

template <>
struct Printer<OutputSpec>
{
    template <typename T>
    static void Output(Orc::Text::Tree<T>& root, const OutputSpec& output)
    {
        if (output.Path.empty() && output.Type != OutputSpec::Kind::SQL)
        {
            Print(root, kStringEmpty);
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

        Print(root, outputPath);

        if (output.UploadOutput != nullptr)
        {
            PrintValue(root, L"Upload configuration:", *(output.UploadOutput));
        }
    }
};

}  // namespace Text
}  // namespace Orc
