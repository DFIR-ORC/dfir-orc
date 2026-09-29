//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
// Whether this build may log detail that a shipped build withholds.
//
// Some values are deliberately kept out of a log because they are server-supplied, secret, or both: a
// <response> capture holding a session token, a credential, a full request path with such a value
// substituted into it. Withholding them costs diagnostic power, and the place that hurts is exactly
// where a remote debugging session needs it. This is the one switch that trades the two, so that a
// question about what a build logs has a single answer rather than one per call site.
//
// It is ON for the Debug and RelWithDebInfo configurations, which exist to be diagnosed, and OFF for
// MinSizeRel and Release, which are what ships. -DORC_VERBOSE_DIAGNOSTICS=ON forces it on everywhere;
// cmake/Orc.cmake sets the definition and warns when it does.
//
// READ THIS BEFORE USING IT. "More logging" and "logging things we withhold for confidentiality" are
// not the same request, and this macro grants the second. Use it only where the extra detail really is
// sensitive. Plain noise belongs behind a log LEVEL (Log::Debug and friends), which an operator can
// turn up in the field without handing anyone a secret. A RelWithDebInfo binary is a diagnostic
// artefact: do not hand one to a third party and expect its log to be shareable.
//
// Three ways to test it, all equivalent:
//
//   #if ORC_HAS_VERBOSE_DIAGNOSTICS            // whole blocks, nothing else compiled
//       Log::Debug(L"secret detail: {}", secret);
//   #endif
//
//   if constexpr (Orc::kVerboseDiagnostics)    // inside a function, no preprocessor
//   {
//   }
//
//   Log::Debug(L"path '{}'", ORC_DIAGNOSTIC_VALUE(resolved, redacted));   // pick one of two values
//
// Prefer ORC_DIAGNOSTIC_VALUE where a withheld form exists: a build that hides the sensitive value
// still logs the line, so the log keeps its shape and a reader is not left wondering what happened.
//
#pragma once

#ifdef ORC_VERBOSE_DIAGNOSTICS
#    define ORC_HAS_VERBOSE_DIAGNOSTICS 1
#else
#    define ORC_HAS_VERBOSE_DIAGNOSTICS 0
#endif

// Textual selection rather than a ternary on a constant: the form this build does not use is not
// compiled at all, so a sensitive value cannot reach a shipped binary's string table through a branch
// the optimizer was trusted to drop.
#if ORC_HAS_VERBOSE_DIAGNOSTICS
#    define ORC_DIAGNOSTIC_VALUE(verbose, withheld) (verbose)
#else
#    define ORC_DIAGNOSTIC_VALUE(verbose, withheld) (withheld)
#endif

namespace Orc {

// The same answer as a constant, for `if constexpr` and for anything that wants a value rather than a
// preprocessor branch.
constexpr bool kVerboseDiagnostics = ORC_HAS_VERBOSE_DIAGNOSTICS != 0;

}  // namespace Orc
