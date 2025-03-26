//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "StdAfx.h"

#include <type_traits>

#include "FatFileInfo.h"
#include "FSUtils.h"

#include "TableOutputWriter.h"

using namespace Orc;

const ColumnNameDef FatFileInfo::g_FatColumnNames[] = {
    {Intentions::FILEINFO_COMPUTERNAME, L"ComputerName", L"Computer name", 0L},
    {Intentions::FILEINFO_VOLUMEID, L"VolumeID", L"Volume ID", 0L},

    {Intentions::FILEINFO_FILENAME, L"File", L"Name of the file", 0L},
    {Intentions::FILEINFO_PARENTNAME, L"ParentName", L"Name of the parent folder", 0L},
    {Intentions::FILEINFO_FULLNAME, L"FullName", L"Full pathname", 0L},
    {Intentions::FILEINFO_EXTENSION, L"Extension", L"Optional filename extension (splitpath)", 0L},

    {Intentions::FILEINFO_FILESIZE, L"SizeInBytes", L"File size in bytes", FILE_READ_EA | FILE_READ_ATTRIBUTES},
    {Intentions::FILEINFO_ATTRIBUTES,
     L"Attributes",
     L"FAT file system attributes",
     FILE_READ_EA | FILE_READ_ATTRIBUTES},

    {Intentions::FILEINFO_CREATIONDATE, L"CreationDate", L"File creation date", FILE_READ_EA | FILE_READ_ATTRIBUTES},
    {Intentions::FILEINFO_LASTMODDATE,
     L"LastModificationDate",
     L"File last write date",
     FILE_READ_EA | FILE_READ_ATTRIBUTES},
    {Intentions::FILEINFO_LASTACCDATE,
     L"LastAccessDate",
     L"File last read date (pre-vista)",
     FILE_READ_EA | FILE_READ_ATTRIBUTES},

    {Intentions::FILEINFO_RECORDINUSE, L"RecordInUse", L"Indicates if the record is in use (or freed/deleted)", 0L},
    {Intentions::FILEINFO_SHORTNAME, L"ShortName", L"Short Name (8.3) if any", 0L},

    {Intentions::FILEINFO_MD5, L"MD5", L"Cryptographic MD5 hash (in hex)", FILE_READ_ATTRIBUTES | FILE_READ_DATA},
    {Intentions::FILEINFO_SHA1, L"SHA1", L"Cryptographic SHA1 hash (in hex)", FILE_READ_ATTRIBUTES | FILE_READ_DATA},
    {Intentions::FILEINFO_FIRST_BYTES,
     L"FirstBytes",
     L"First bytes of the data stream",
     FILE_READ_ATTRIBUTES | FILE_READ_DATA},

    {Intentions::FILEINFO_VERSION, L"Version", L"VersionInfo file version", 0L},
    {Intentions::FILEINFO_COMPANY, L"CompanyName", L"VersionInfo company name", 0L},
    {Intentions::FILEINFO_PRODUCT, L"ProductName", L"VersionInfo product name", 0L},
    {Intentions::FILEINFO_ORIGINALNAME, L"OriginalFileName", L"VersionInfo original file name", 0L},

    {Intentions::FILEINFO_PLATFORM, L"Platform", L"PE Header platform", FILE_READ_DATA},
    {Intentions::FILEINFO_TIMESTAMP, L"TimeStamp", L"PE Header timestamp", FILE_READ_DATA},

    {Intentions::FILEINFO_SUBSYSTEM, L"SubSystem", L"PE HEader SubSystem", FILE_READ_DATA},
    {Intentions::FILEINFO_FILETYPE, L"FileType", L"VersionInfo type", 0L},
    {Intentions::FILEINFO_FILEOS, L"FileOS", L"VersionInfo OS tag", 0L},

    {Intentions::FILEINFO_SHA256, L"SHA256", L"SHA256 ", 0L},
    {Intentions::FILEINFO_PE_SHA1, L"PeSHA1", L"SHA1 of PE file", 0L},
    {Intentions::FILEINFO_PE_SHA256, L"PeSHA256", L"SHA256 of a PE file", 0L},

    {Intentions::FILEINFO_SECURITY_DIRECTORY,
     L"SecurityDirectory",
     L"Base64 encoded security directory of the PE file (if present)",
     0L},

    {Intentions::FILEINFO_AUTHENTICODE_STATUS,
     L"AuthenticodeStatus",
     L"Status of authenticode signature (SignedVerified, SignedNotVerified, NotSigned)",
     0L},
    {Intentions::FILEINFO_AUTHENTICODE_SIGNER, L"AuthenticodeSigner", L"Signer of this file signature", 0L},
    {Intentions::FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT,
     L"AuthenticodeSignerThumbprint",
     L"Thumbprint of the signer of this file signer",
     0L},
    {Intentions::FILEINFO_AUTHENTICODE_CA, L"AuthenticodeCA", L"Authority of signer of this file signature", 0L},
    {Intentions::FILEINFO_AUTHENTICODE_CA_THUMBPRINT,
     L"AuthenticodeCAThumbprint",
     L"Thumbprint of the authority of this file signer",
     0L},

    {Intentions::FILEINFO_PE_MD5, L"PeMD5", L"MD5 of PE file", 0L},

    {Intentions::FILEINFO_NONE, NULL, NULL, 0L}};

const ColumnNameDef FatFileInfo::g_FatAliasNames[] = {
    {Intentions::FILEINFO_PARENTNAME | Intentions::FILEINFO_FILENAME | Intentions::FILEINFO_EXTENSION
         | Intentions::FILEINFO_ATTRIBUTES | Intentions::FILEINFO_FILESIZE | Intentions::FILEINFO_CREATIONDATE
         | Intentions::FILEINFO_LASTMODDATE | Intentions::FILEINFO_LASTACCDATE | Intentions::FILEINFO_RECORDINUSE,
     L"Default",
     NULL,
     0L},
    {Intentions::FILEINFO_PARENTNAME | Intentions::FILEINFO_FILENAME | Intentions::FILEINFO_SHORTNAME
         | Intentions::FILEINFO_EXTENSION | Intentions::FILEINFO_ATTRIBUTES | Intentions::FILEINFO_FILESIZE
         | Intentions::FILEINFO_CREATIONDATE | Intentions::FILEINFO_LASTMODDATE | Intentions::FILEINFO_LASTACCDATE
         | Intentions::FILEINFO_PLATFORM | Intentions::FILEINFO_TIMESTAMP | Intentions::FILEINFO_SUBSYSTEM
         | Intentions::FILEINFO_FILEOS | Intentions::FILEINFO_FILETYPE | Intentions::FILEINFO_VERSION
         | Intentions::FILEINFO_COMPANY | Intentions::FILEINFO_PRODUCT | Intentions::FILEINFO_ORIGINALNAME
         | Intentions::FILEINFO_MD5 | Intentions::FILEINFO_SHA1,
     L"DeepScan",
     NULL,
     0L},
    {Intentions::FILEINFO_PLATFORM | Intentions::FILEINFO_TIMESTAMP | Intentions::FILEINFO_SUBSYSTEM
         | Intentions::FILEINFO_FILEOS | Intentions::FILEINFO_FILETYPE | Intentions::FILEINFO_VERSION
         | Intentions::FILEINFO_COMPANY | Intentions::FILEINFO_PRODUCT | Intentions::FILEINFO_ORIGINALNAME,
     L"Details"},
    {Intentions::FILEINFO_MD5 | Intentions::FILEINFO_SHA1 | Intentions::FILEINFO_SHA256, L"Hashes", NULL, 0L},
    {Intentions::FILEINFO_PE_MD5 | Intentions::FILEINFO_PE_SHA1 | Intentions::FILEINFO_PE_SHA256,
     L"PeHashes",
     NULL,
     0L},
    {Intentions::FILEINFO_CREATIONDATE | Intentions::FILEINFO_LASTMODDATE | Intentions::FILEINFO_LASTACCDATE,
     L"Dates",
     NULL,
     0L},
    {Intentions::FILEINFO_AUTHENTICODE_STATUS | Intentions::FILEINFO_AUTHENTICODE_SIGNER
         | Intentions::FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT | Intentions::FILEINFO_AUTHENTICODE_CA
         | Intentions::FILEINFO_AUTHENTICODE_CA_THUMBPRINT,
     L"Authenticode",
     NULL,
     0L},
    {Intentions::FILEINFO_ALL, L"All", NULL, 0L},
    {Intentions::FILEINFO_NONE, NULL, NULL, 0L}};
