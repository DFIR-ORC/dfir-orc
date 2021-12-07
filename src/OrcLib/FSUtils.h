//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            Yann SALAUN
//
#pragma once

#include "Utils/EnumFlags.h"

#pragma managed(push, off)

namespace Orc {

namespace TableOutput {
class IOutput;
}
using ITableOutput = TableOutput::IOutput;

static constexpr const unsigned __int64 One = 1;

// Usefull with http://blogs.msdn.com/vcblog/archive/2008/03/10/visual-studio-2008-enum-bit-flags-visualization.aspx
enum class Intentions : DWORDLONG
{
    FILEINFO_NONE = 0,

    FILEINFO_COMPUTERNAME = (One),

    FILEINFO_VOLUMEID = (One << 1),

    FILEINFO_FILENAME = (One << 2),
    FILEINFO_PARENTNAME = (One << 3),
    FILEINFO_FULLNAME = (One << 4),

    FILEINFO_EXTENSION = (One << 5),

    FILEINFO_FILESIZE = (One << 6),
    FILEINFO_ATTRIBUTES = (One << 7),

    FILEINFO_CREATIONDATE = (One << 8),
    FILEINFO_LASTMODDATE = (One << 9),
    FILEINFO_LASTACCDATE = (One << 10),
    FILEINFO_LASTATTRCHGDATE = (One << 11),

    FILEINFO_FN_CREATIONDATE = (One << 12),
    FILEINFO_FN_LASTMODDATE = (One << 13),
    FILEINFO_FN_LASTACCDATE = (One << 14),
    FILEINFO_FN_LASTATTRMODDATE = (One << 15),

    FILEINFO_USN = (One << 16),
    FILEINFO_FRN = (One << 17),
    FILEINFO_PARENTFRN = (One << 18),

    FILEINFO_EXTENDED_ATTRIBUTE = (One << 19),

    FILEINFO_ADS = (One << 20),

    FILEINFO_FILENAMEID = (One << 21),
    FILEINFO_DATAID = (One << 22),

    FILEINFO_RECORDINUSE = (One << 23),

    FILEINFO_SHORTNAME = (One << 24),

    FILEINFO_MD5 = (One << 25),
    FILEINFO_SHA1 = (One << 26),
    FILEINFO_FIRST_BYTES = (One << 27),

    FILEINFO_OWNERID = (One << 28),
    FILEINFO_OWNERSID = (One << 29),
    FILEINFO_OWNER = (One << 30),

    FILEINFO_VERSION = (One << 31),
    FILEINFO_COMPANY = (One << 32),
    FILEINFO_PRODUCT = (One << 33),
    FILEINFO_ORIGINALNAME = (One << 34),

    FILEINFO_PLATFORM = (One << 35),
    FILEINFO_TIMESTAMP = (One << 36),

    FILEINFO_SUBSYSTEM = (One << 37),
    FILEINFO_FILETYPE = (One << 38),
    FILEINFO_FILEOS = (One << 39),

    FILEINFO_FILENAMEFLAGS = (One << 40),

    FILEINFO_SHA256 = (One << 41),
    FILEINFO_PE_SHA1 = (One << 42),
    FILEINFO_PE_SHA256 = (One << 43),

    FILEINFO_SEC_DESCR_ID = (One << 44),

    FILEINFO_EA_SIZE = (One << 45),

    FILEINFO_SECURITY_DIRECTORY = (One << 46),

    FILEINFO_AUTHENTICODE_STATUS = (One << 47),
    FILEINFO_AUTHENTICODE_SIGNER = (One << 48),
    FILEINFO_AUTHENTICODE_SIGNER_THUMBPRINT = (One << 49),
    FILEINFO_AUTHENTICODE_CA = (One << 50),
    FILEINFO_AUTHENTICODE_CA_THUMBPRINT = (One << 51),

    FILEINFO_PE_MD5 = (One << 52),

    FILEINFO_FILENAME_IDX = (One << 53),
    FILEINFO_DATA_IDX = (One << 54),

    FILEINFO_SNAPSHOTID = (One << 55),

    FILEINFO_SSDEEP = (One << 56),
    FILEINFO_TLSH = (One << 57),

    FILEINFO_SIGNED_HASH = (One << 58),

    FILEINFO_SECURITY_DIRECTORY_SIZE = (One << 59),
    FILEINFO_SECURITY_DIRECTORY_SIGNATURE_SIZE = (One << 60),

    FILEINFO_ALL = (unsigned long long)-1
};

ENABLE_BITMASK_OPERATORS(Intentions);

class IIntentionsHandler
{
public:
    virtual ~IIntentionsHandler() {}
    virtual HRESULT HandleIntentions(const Intentions& itention, ITableOutput& output) PURE;
};

enum FilterType
{
    FILEFILTER_NONE = 0,

    FILEFILTER_EXTBINARY = (1 << 1),
    FILEFILTER_EXTSCRIPT = (1 << 2),
    FILEFILTER_EXTARCHIVE = (1 << 3),
    FILEFILTER_EXTCUSTOM = (1 << 4),

    FILEFILTER_SIZELESS = (1 << 5),
    FILEFILTER_SIZEMORE = (1 << 6),

    FILEFILTER_VERSIONINFO = (1 << 7),
    FILEFILTER_PEHEADER = (1 << 8)
};

struct Filter
{
    bool bInclude;
    FilterType type;
    union
    {
        LARGE_INTEGER size;
        WCHAR** extcustom;
    } filterdata;
    Intentions intent;
};

struct ColumnNameDef
{
    Intentions dwIntention;
    WCHAR* szColumnName;
    WCHAR* szColumnDesc;
    DWORD dwRequiredAccess;
};
}  // namespace Orc
#pragma managed(pop)
