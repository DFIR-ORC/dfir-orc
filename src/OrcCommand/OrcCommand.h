//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#ifdef ORCUTILS_EXPORTS
#    define ORCUTILS_API __declspec(dllexport)
#elif ORCUTILS_IMPORTS
#    define ORCUTILS_API __declspec(dllimport)
#else  // STATIC linking
#    define ORCUTILS_API
#endif
