//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#pragma once

#include <map>
#include <string>

#include "Network/Http/HttpClient.h"
#include "RESTStep.h"

namespace Orc {

// Apply a <rest> step's <response> captures into 'captures' (name -> value), reusable by later steps as
// {name}. Each capture reads one bounded source - a response header, a DAV property from a PROPFIND 207
// body, or a hidden <input>'s value scanned out of an HTML body - and falls back to its configured
// default on a 404, an absent value, a non-UTF-8 body, or a value that fails IsAcceptableCaptureValue
// (length, control characters, and for form_input, non-ASCII). Reserved 'upload.' names are skipped so a
// server value can never populate an engine built-in.
//
// A form_input capture with no value and no configured default is a HARD failure: this function returns
// E_FAIL rather than silently substituting an empty string, because a login step that quietly sends an
// empty token fails later with no clue why. Header and dav_property captures are NOT held to this rule -
// they keep the fall-back-to-default behaviour, since an absent header or property is routinely a
// legitimate "nothing to report" (e.g. a resume dialog's size header).
HRESULT ApplyResponseCaptures(
    const RESTStep& step, const HttpResponse& response, std::map<std::wstring, std::wstring>& captures);

}  // namespace Orc
