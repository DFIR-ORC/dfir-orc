//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <streambuf>

namespace Orc {

class WideStreambufConverter : public std::basic_streambuf<wchar_t>
{
public:
    explicit WideStreambufConverter(std::basic_streambuf<char>* output);

protected:
    int_type overflow(int_type c) override;
    int sync() override;

private:
    std::basic_streambuf<char>* m_output;
};

}  // namespace Orc
