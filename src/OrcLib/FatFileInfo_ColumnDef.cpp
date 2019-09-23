//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "StdAfx.h"

#include "FatFileInfo.h"
#include "FSUtils.h"

#include "TableOutputWriter.h"

using namespace Orc;

const ORCLIB_API ColumnNameDef FatFileInfo::g_FatColumnNames[] = {
    {FILEINFO_COMPUTERNAME, L"ComputerName", L"Computer name", 0L},
    {FILEINFO_VOLUMEID, L"VolumeID", L"Volume ID", 0L},

    {FILEINFO_FILENAME, L"File", L"Name of the file", 0L},
    {FILEINFO_PARENTNAME, L"ParentName", L"Name of the parent folder", 0L},
    {FILEINFO_FULLNAME, L"FullName", L"Full pathname", 0L},
    {FILEINFO_EXTENSION, L"Extension", L"Optional filename extension (splitpath)", 0L},

    {FILEINFO_FILESIZE, L"SizeInBytes", L"File size in bytes", FILE_READ_EA | FILE_READ_ATTRIBUTES},
    {FILEINFO_ATTRIBUTES, L"Attributes", L"FAT file system attributes", FILE_READ_EA | FILE_READ_ATTRIBUTES},

    {FILEINFO_CREATIONDATE, L"CreationDate", L"File creation date", FILE_READ_EA | FILE_READ_ATTRIBUTES},
    {FILEINFO_LASTMODDATE, L"LastModificationDate", L"File last write date", FILE_READ_EA | FILE_READ_ATTRIBUTES},
    {FILEINFO_LASTACCDATE, L"LastAccessDate", L"File last read date (pre-vista)", FILE_READ_EA | FILE_READ_ATTRIBUTES},

    {FILEINFO_RECORDINUSE, L"RecordInUse", L"Indicates if the record is in use (or freed/deleted)", 0L},
    {FILEINFO_SHORTNAME, L"ShortName", L"Short Name (8.3) if any", 0L},

    {FILEINFO_MD5, L"MD5", L"Cryptographic MD5 hash (in hex)", FILE_READ_ATTRIBUTES | FILE_READ_DATA},
    {FILEINFO_SHA1, L"SHA-1", L"Cryptographic SHA1 hash (in hex)", FILE_READ_ATTRIBUTES | FILE_READ_DATA},
    {FILEINFO_FIRST_BYTES, L"FirstBytes", L"First bytes of the data stream", FILE_READ_ATTRIBUTES | FILE_READ_DATA},

    {FILEINFO_VERSION, L"Version", L"VersionInfo file version", 0L},
    {FILEINFO_COMPANY, L"CompanyName", L"VersionInfo company name", 0L},
    {FILEINFO_PRODUCT, L"ProductName", L"VersionInfo product name", 0L},
    {FILEINFO_ORIGINALNAME, L"OriginalFileName", L"VersionInfo original file name", 0L},

    {FILEINFO_PLATFORM, L"Platform", L"PE Header platform", FILE_READ_DATA},
    {FILEINFO_TIMESTAMP, L"TimeStamp", L"PE Header timestamp", FILE_READ_DATA},

    {FILEINFO_SUBSYSTEM, L"SubSystem", L"PE HEader SubSystem", FILE_READ_DATA},
    {FILEINFO_FILETYPE, L"FileType", L"VersionInfo type", 0L},
    {FILEINFO_FILEOS, L"FileOS", L"VersionInfo OS tag", 0L},

    {FILEINFO_SHA256, L"SHA256", L"SHA256 ", 0L},
    {FILEINFO_PE_SHA1, L"PeSHA1", L"SHA1 of PE file", 0L},
    {FILEINFO_PE_SHA256, L"PeSHA256", L"SHA256 of a PE file", 0L},

    {FILEINFO_SECURITY_DIRECTORY,
     L"SecurityDirectory",
     L"Base64 encoded security directory of the PE file (if present)",
     0L},

    {FILEINFO_AUTHENTICODE_STATUS,
     L"AuthenticodeStatus",
     L"Status of this file regarding authenticode signature (SignedVerified,SignedNotVerified,NotSigned",
     0L},
    {FILEINFO_AUTHENTICODE_SIGNER, L"AuthenticodeSigner", L"Signer of this file's signature", 0L},
    {FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT,
     L"AuthenticodeSignerThumbprint",
     L"Thumbprint of the signer of this file's signer",
     0L},
    {FILEINFO_AUTHENTICODE_CA, L"AuthenticodeCA", L"Authority of signer of this file's signature", 0L},
    {FILEINFO_AUTHENTICODE_CA_THUMBPRINT,
     L"AuthenticodeCAThumbprint",
     L"Thumbprint of the authority of the signer of this file's signer",
     0L},

    {FILEINFO_PE_MD5, L"PeMD5", L"MD5 of PE file", 0L},

    {FILEINFO_NONE, NULL, NULL, 0L}};

const ORCLIB_API ColumnNameDef FatFileInfo::g_FatAliasNames[] = {
    {static_cast<Intentions>(
         FILEINFO_PARENTNAME | FILEINFO_FILENAME | FILEINFO_EXTENSION | FILEINFO_ATTRIBUTES | FILEINFO_FILESIZE
         | FILEINFO_CREATIONDATE | FILEINFO_LASTMODDATE | FILEINFO_LASTACCDATE | FILEINFO_RECORDINUSE),
     L"Default",
     NULL,
     0L},
    {static_cast<Intentions>(
         FILEINFO_PARENTNAME | FILEINFO_FILENAME | FILEINFO_SHORTNAME | FILEINFO_EXTENSION | FILEINFO_ATTRIBUTES
         | FILEINFO_FILESIZE | FILEINFO_CREATIONDATE | FILEINFO_LASTMODDATE | FILEINFO_LASTACCDATE | FILEINFO_PLATFORM
         | FILEINFO_TIMESTAMP | FILEINFO_SUBSYSTEM | FILEINFO_FILEOS | FILEINFO_FILETYPE | FILEINFO_VERSION
         | FILEINFO_COMPANY | FILEINFO_PRODUCT | FILEINFO_ORIGINALNAME | FILEINFO_MD5 | FILEINFO_SHA1),
     L"DeepScan",
     NULL,
     0L},
    {static_cast<Intentions>(
         FILEINFO_PLATFORM | FILEINFO_TIMESTAMP | FILEINFO_SUBSYSTEM | FILEINFO_FILEOS | FILEINFO_FILETYPE
         | FILEINFO_VERSION | FILEINFO_COMPANY | FILEINFO_PRODUCT | FILEINFO_ORIGINALNAME),
     L"Details"},
    {static_cast<Intentions>(FILEINFO_MD5 | FILEINFO_SHA1 | FILEINFO_SHA256), L"Hashes", NULL, 0L},
    {static_cast<Intentions>(FILEINFO_PE_MD5 | FILEINFO_PE_SHA1 | FILEINFO_PE_SHA256), L"PeHashes", NULL, 0L},
    {static_cast<Intentions>(FILEINFO_CREATIONDATE | FILEINFO_LASTMODDATE | FILEINFO_LASTACCDATE), L"Dates", NULL, 0L},
    {static_cast<Intentions>(
         FILEINFO_AUTHENTICODE_STATUS | FILEINFO_AUTHENTICODE_SIGNER | FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT
         | FILEINFO_AUTHENTICODE_CA | FILEINFO_AUTHENTICODE_CA_THUMBPRINT),
     L"Authenticode",
     NULL,
     0L},
    {FILEINFO_ALL, L"All", NULL, 0L},
    {FILEINFO_NONE, NULL, NULL, 0L}};
