//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "NtfsFileInfo.h"

#include "TableOutputWriter.h"

using namespace Orc;

const ORCLIB_API ColumnNameDef NtfsFileInfo::g_NtfsColumnNames[] = {
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
    {Intentions::FILEINFO_LASTATTRCHGDATE, L"LastAttrChangeDate", L"Last Attribute Change Date", 0L},

    {Intentions::FILEINFO_FN_CREATIONDATE,
     L"FileNameCreationDate",
     L"Indicates when this file created using this name",
     0L},
    {Intentions::FILEINFO_FN_LASTMODDATE,
     L"FileNameLastModificationDate",
     L"Indicates when this file data was last modified using this name",
     0L},
    {Intentions::FILEINFO_FN_LASTACCDATE,
     L"FileNameLastAccessDate",
     L"Indicates when this file was last read using this name",
     0L},
    {Intentions::FILEINFO_FN_LASTATTRMODDATE,
     L"FileNameLastAttrModificationDate",
     L"Indicates when this file's attributes were last modified using this name",
     0L},

    {Intentions::FILEINFO_USN, L"USN", L"Update Sequence Number", 0L},
    {Intentions::FILEINFO_FRN, L"FRN", L"File Reference Number", 0L},
    {Intentions::FILEINFO_PARENTFRN, L"ParentFRN", L"Parent Folder Reference Number", 0L},

    {Intentions::FILEINFO_EXTENDED_ATTRIBUTE, L"ExtendedAttribute", L"Extended Attribute Information", 0L},

    {Intentions::FILEINFO_ADS, L"ADS", L"Alternate Data Stream Information", FILE_READ_ATTRIBUTES},

    {Intentions::FILEINFO_FILENAMEID, L"FilenameID", L"$FILE_NAME Attribute Instance ID", 0L},
    {Intentions::FILEINFO_DATAID, L"DataID", L"$DATA Attribute Instance ID", 0L},

    {Intentions::FILEINFO_RECORDINUSE, L"RecordInUse", L"Indicates if the record is in use (or freed/deleted)", 0L},

    {Intentions::FILEINFO_SHORTNAME, L"ShortName", L"Short Name (8.3) if any", 0L},

    {Intentions::FILEINFO_MD5, L"MD5", L"Cryptographic MD5 hash (in hex)", FILE_READ_ATTRIBUTES | FILE_READ_DATA},
    {Intentions::FILEINFO_SHA1, L"SHA1", L"Cryptographic SHA1 hash (in hex)", FILE_READ_ATTRIBUTES | FILE_READ_DATA},
    {Intentions::FILEINFO_FIRST_BYTES,
     L"FirstBytes",
     L"First bytes of the data stream",
     FILE_READ_ATTRIBUTES | FILE_READ_DATA},

    {Intentions::FILEINFO_OWNERID, L"OwnerId", L"File owner's unique ID", FILE_READ_ATTRIBUTES | FILE_READ_DATA},
    {Intentions::FILEINFO_OWNERSID, L"OwnerSid", L"File owner's SID", STANDARD_RIGHTS_READ},
    {Intentions::FILEINFO_OWNER, L"Owner", L"File owner", STANDARD_RIGHTS_READ},

    {Intentions::FILEINFO_VERSION, L"Version", L"VersionInfo file version", 0L},
    {Intentions::FILEINFO_COMPANY, L"CompanyName", L"VersionInfo company name", 0L},
    {Intentions::FILEINFO_PRODUCT, L"ProductName", L"VersionInfo product name", 0L},
    {Intentions::FILEINFO_ORIGINALNAME, L"OriginalFileName", L"VersionInfo original file name", 0L},

    {Intentions::FILEINFO_PLATFORM, L"Platform", L"PE Header platform", FILE_READ_DATA},
    {Intentions::FILEINFO_TIMESTAMP, L"TimeStamp", L"PE Header timestamp", FILE_READ_DATA},

    {Intentions::FILEINFO_SUBSYSTEM, L"SubSystem", L"PE HEader SubSystem", FILE_READ_DATA},
    {Intentions::FILEINFO_FILETYPE, L"FileType", L"VersionInfo type", 0L},
    {Intentions::FILEINFO_FILEOS, L"FileOS", L"VersionInfo OS tag", 0L},

    {Intentions::FILEINFO_FILENAMEFLAGS,
     L"FilenameFlags",
     L"$FILE_NAME Attribute Flags (POSIX=0, WIN32=1, DOS83=2)",
     0L},

    {Intentions::FILEINFO_SHA256, L"SHA256", L"SHA256 ", 0L},
    {Intentions::FILEINFO_PE_SHA1, L"PeSHA1", L"SHA1 of PE file", 0L},
    {Intentions::FILEINFO_PE_SHA256, L"PeSHA256", L"SHA256 of a PE file", 0L},

    {Intentions::FILEINFO_SEC_DESCR_ID, L"SecDescrID", L"ID of security descriptor for the file", 0L},
    {Intentions::FILEINFO_EA_SIZE, L"EASize", L"Size in bytes of the extended attributes (if present)", 0L},
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
     L"Thumbprint of the authority of the signer of this file signer",
     0L},

    {Intentions::FILEINFO_PE_MD5, L"PeMD5", L"MD5 of PE file", 0L},

    {Intentions::FILEINFO_FILENAME_IDX, L"FilenameIndex", L"Index of this $FILE_NAME in this record", 0L},
    {Intentions::FILEINFO_DATA_IDX, L"DataIndex", L"Index of this $DATA in this record", 0L},

    {Intentions::FILEINFO_SNAPSHOTID, L"SnapshotID", L"Snapshot associated with this entry", 0L},

    {Intentions::FILEINFO_SSDEEP, L"SSDeep", L"Fuzzy hash SSDEEP", 0L},
    {Intentions::FILEINFO_TLSH, L"TLSH", L"TrendMicro's TLSH", 0L},

    {Intentions::FILEINFO_SIGNED_HASH, L"SignedHash", L"The signed hash inside the security directory of the PE", 0L},

    {Intentions::FILEINFO_SECURITY_DIRECTORY_SIZE, L"SecurityDirectorySize", L"The size of the security directory", 0L},
    {Intentions::FILEINFO_SECURITY_DIRECTORY_SIGNATURE_SIZE,
     L"SecurityDirectorySignatureSize",
     L"The size of the signature inside the security directory",
     0L},

    {Intentions::FILEINFO_NONE, NULL, NULL, 0L}};

const ORCLIB_API ColumnNameDef NtfsFileInfo::g_NtfsAliasNames[] = {
    {static_cast<Intentions>(
         Intentions::FILEINFO_PARENTNAME | Intentions::FILEINFO_FILENAME | Intentions::FILEINFO_EXTENSION
         | Intentions::FILEINFO_ATTRIBUTES | Intentions::FILEINFO_FILESIZE | Intentions::FILEINFO_CREATIONDATE
         | Intentions::FILEINFO_LASTMODDATE | Intentions::FILEINFO_LASTACCDATE | Intentions::FILEINFO_LASTATTRCHGDATE
         | Intentions::FILEINFO_FN_CREATIONDATE | Intentions::FILEINFO_FN_LASTACCDATE
         | Intentions::FILEINFO_FN_LASTATTRMODDATE | Intentions::FILEINFO_FN_LASTMODDATE
         | Intentions::FILEINFO_FILENAMEFLAGS | Intentions::FILEINFO_RECORDINUSE | Intentions::FILEINFO_FILENAMEID
         | Intentions::FILEINFO_DATAID | Intentions::FILEINFO_EXTENDED_ATTRIBUTE | Intentions::FILEINFO_ADS
         | Intentions::FILEINFO_OWNERID | Intentions::FILEINFO_USN | Intentions::FILEINFO_FRN
         | Intentions::FILEINFO_PARENTFRN | Intentions::FILEINFO_SEC_DESCR_ID | Intentions::FILEINFO_FILENAME_IDX
         | Intentions::FILEINFO_DATA_IDX | Intentions::FILEINFO_SNAPSHOTID),
     L"Default",
     NULL,
     0L},
    {static_cast<Intentions>(
         Intentions::FILEINFO_PARENTNAME | Intentions::FILEINFO_FILENAME | Intentions::FILEINFO_SHORTNAME
         | Intentions::FILEINFO_EXTENSION | Intentions::FILEINFO_ATTRIBUTES | Intentions::FILEINFO_FILESIZE
         | Intentions::FILEINFO_CREATIONDATE | Intentions::FILEINFO_LASTMODDATE | Intentions::FILEINFO_LASTACCDATE
         | Intentions::FILEINFO_OWNER | Intentions::FILEINFO_PLATFORM | Intentions::FILEINFO_TIMESTAMP
         | Intentions::FILEINFO_SUBSYSTEM | Intentions::FILEINFO_FILEOS | Intentions::FILEINFO_FILETYPE
         | Intentions::FILEINFO_VERSION | Intentions::FILEINFO_COMPANY | Intentions::FILEINFO_PRODUCT
         | Intentions::FILEINFO_ORIGINALNAME | Intentions::FILEINFO_MD5 | Intentions::FILEINFO_SHA1),
     L"DeepScan",
     NULL,
     0L},
    {static_cast<Intentions>(
         Intentions::FILEINFO_PLATFORM | Intentions::FILEINFO_TIMESTAMP | Intentions::FILEINFO_SUBSYSTEM
         | Intentions::FILEINFO_FILEOS | Intentions::FILEINFO_FILETYPE | Intentions::FILEINFO_VERSION
         | Intentions::FILEINFO_COMPANY | Intentions::FILEINFO_PRODUCT | Intentions::FILEINFO_ORIGINALNAME),
     L"Details"},
    {static_cast<Intentions>(Intentions::FILEINFO_MD5 | Intentions::FILEINFO_SHA1 | Intentions::FILEINFO_SHA256),
     L"Hashes",
     NULL,
     0L},
    {Intentions::FILEINFO_SSDEEP | Intentions::FILEINFO_TLSH, L"Fuzzy", NULL, 0L},
    {Intentions::FILEINFO_PE_MD5 | Intentions::FILEINFO_PE_SHA1 | Intentions::FILEINFO_PE_SHA256,
     L"PeHashes",
     NULL,
     0L},
    {static_cast<Intentions>(
         Intentions::FILEINFO_CREATIONDATE | Intentions::FILEINFO_LASTMODDATE | Intentions::FILEINFO_LASTACCDATE
         | Intentions::FILEINFO_LASTATTRCHGDATE | Intentions::FILEINFO_FN_CREATIONDATE
         | Intentions::FILEINFO_FN_LASTACCDATE | Intentions::FILEINFO_FN_LASTATTRMODDATE
         | Intentions::FILEINFO_FN_LASTMODDATE),
     L"Dates",
     NULL,
     0L},
    {Intentions::FILEINFO_USN | Intentions::FILEINFO_FRN | Intentions::FILEINFO_PARENTFRN, L"RefNums", NULL, 0L},
    {Intentions::FILEINFO_AUTHENTICODE_STATUS | Intentions::FILEINFO_AUTHENTICODE_SIGNER
         | Intentions::FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT | Intentions::FILEINFO_AUTHENTICODE_CA
         | Intentions::FILEINFO_AUTHENTICODE_CA_THUMBPRINT | Intentions::FILEINFO_SIGNED_HASH,
     L"Authenticode",
     NULL,
     0L},
    {Intentions::FILEINFO_ALL, L"All", NULL, 0L},
    {Intentions::FILEINFO_NONE, NULL, NULL, 0L}};
