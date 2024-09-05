//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ResurrectRecordsMode.h"

#include <boost/algorithm/string.hpp>

#include "Log/Log.h"

namespace {

using namespace std::string_view_literals;

constexpr auto kNo = "No"sv;
constexpr auto kYes = "Yes"sv;
constexpr auto kResident = "Resident"sv;

constexpr auto kNoW = L"No"sv;
constexpr auto kYesW = L"Yes"sv;
constexpr auto kResidentW = L"Resident"sv;

using ResurrectRecordsModeEntry = std::tuple<Orc::ResurrectRecordsMode, std::string_view, std::wstring_view>;

constexpr std::array kModes = {
    ResurrectRecordsModeEntry {
        Orc::ResurrectRecordsMode::kNo,
        kNo,
        kNoW,
    },
    ResurrectRecordsModeEntry {
        Orc::ResurrectRecordsMode::kYes,
        kYes,
        kYesW,
    },
    ResurrectRecordsModeEntry {Orc::ResurrectRecordsMode::kResident, kResident, kResidentW}};

Orc::Result<ResurrectRecordsModeEntry> Get(Orc::ResurrectRecordsMode mode)
{
    using namespace Orc;

    if (mode >= ResurrectRecordsMode::kCount)
    {
        Log::Debug(
            "Failed 'ResurrectRecordsMode' conversion (value: {})",
            static_cast<std::underlying_type_t<ResurrectRecordsMode>>(mode));

        return std::errc::no_such_file_or_directory;
    }

    const auto& entry = kModes[static_cast<size_t>(mode)];
    if (std::get<0>(entry) != mode)
    {
        return std::errc::not_supported;
    }

    return entry;
}

}  // namespace

namespace Orc {

Result<std::string_view> ToString(ResurrectRecordsMode mode)
{
    auto rv = Get(mode);
    if (!rv)
    {
        return rv.error();
    }

    return std::get<1>(rv.value());
}

Result<std::wstring_view> ToWString(ResurrectRecordsMode mode)
{
    auto rv = Get(mode);
    if (!rv)
    {
        return rv.error();
    }

    return std::get<2>(rv.value());
}

Result<ResurrectRecordsMode> ToResurrectRecordsMode(std::string_view mode)
{
    auto it = std::find_if(std::cbegin(kModes), std::cend(kModes), [&mode](const auto& item) {
        return boost::iequals(std::get<1>(item), mode);
    });

    if (it != std::cend(kModes))
    {
        return std::get<0>(*it);
    }

    return std::errc::no_such_file_or_directory;
}

Result<ResurrectRecordsMode> ToResurrectRecordsMode(std::wstring_view mode)
{
    auto it = std::find_if(std::cbegin(kModes), std::cend(kModes), [&mode](const auto& item) {
        return boost::iequals(std::get<2>(item), mode);
    });

    if (it != std::cend(kModes))
    {
        return std::get<0>(*it);
    }

    return std::errc::no_such_file_or_directory;
}
}  // namespace Orc
