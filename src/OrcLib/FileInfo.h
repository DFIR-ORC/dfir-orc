//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "DataDetails.h"
#include "FSUtils.h"
#include "PEInfo.h"

#include "TableOutput.h"
#include "TableOutputWriter.h"

#include <vector>
#include <memory>

#pragma managed(push, off)

namespace Orc {

class VolumeReader;
using ITableOutput = TableOutput::IOutput;
class Writer;

class ORCLIB_API FileInfo : public IIntentionsHandler
{
public:
    friend class PEInfo;

    FileInfo(
        logger pLog,
        std::wstring strComputerName,
        const std::shared_ptr<VolumeReader>& pVolReader,
        Intentions dwDefaultIntentions,
        const std::vector<Filter>& filters,
        LPCWSTR szFullFileName,
        DWORD dwLen,
        Authenticode& verifytrust,
        bool bWriteErrorCodes);
    virtual ~FileInfo();

    const WCHAR* GetFullName() const { return m_szFullName; }

    virtual HRESULT HandleIntentions(const Intentions& intention, ITableOutput& writer);
    HRESULT WriteFileInformation(
        const logger& pLog,
        const ColumnNameDef columnNames[],
        ITableOutput& output,
        const std::vector<Filter>& filters);

    // abstract methods
    virtual bool IsDirectory() = 0;
    virtual std::shared_ptr<ByteStream> GetFileStream() = 0;
    virtual const std::unique_ptr<DataDetails>& GetDetails() = 0;
    virtual bool ExceedsFileThreshold(DWORD nFileSizeHigh, DWORD nFileSizeLow) = 0;

    // static methods
    static bool HasBinaryExtension(const WCHAR* pszName);
    static bool HasScriptExtension(const WCHAR* pszName);
    static bool HasArchiveExtension(const WCHAR* pszName);
    static bool HasSpecificExtension(const WCHAR* pszName, const WCHAR* pszExtensions[]);

    static Intentions
    GetIntentions(const logger& pLog, const WCHAR* szParams, const ColumnNameDef aliasNames[], const ColumnNameDef columnNames[]);
    static Intentions GetFilterIntentions(const std::vector<Filter>& filters);

    static HRESULT BindColumns(
        const logger& pLog,
        const ColumnNameDef columnNames[],
        Intentions dwIntentions,
        const std::vector<TableOutput::Column>& sqlcolumns,
        TableOutput::IConnectWriter& Writer);

    // check methods
    HRESULT CheckStream();
    HRESULT CheckFirstBytes();
    HRESULT CheckFileHandle();
    HRESULT CheckHash();
    HRESULT CheckFuzzyHash();
    HRESULT CheckAuthenticodeData();

protected:
    // abstract method
    virtual HRESULT Open() = 0;

    DWORD GetRequiredAccessMask(const ColumnNameDef columnNames[]);

    // open methods
    HRESULT OpenFirstBytes();
    virtual HRESULT OpenHash();
    HRESULT OpenCryptoHash(Intentions localIntentions);
    HRESULT OpenFuzzyHash(Intentions localIntentions);
    HRESULT OpenCryptoAndFuzzyHash(Intentions localIntentions);
    HRESULT OpenAuthenticode();

    // write functions
    HRESULT WriteComputerName(ITableOutput& output);
    virtual HRESULT WriteVolumeID(ITableOutput& output);

    HRESULT WriteFullName(ITableOutput& output);
    HRESULT WriteParentName(ITableOutput& output);
    HRESULT WriteExtension(ITableOutput& output);

    virtual HRESULT WriteFileName(ITableOutput& output);
    virtual HRESULT WriteShortName(ITableOutput& output) = 0;

    virtual HRESULT WriteAttributes(ITableOutput& output) = 0;
    virtual HRESULT WriteSizeInBytes(ITableOutput& output) = 0;
    virtual HRESULT WriteRecordInUse(ITableOutput& output) = 0;

    virtual HRESULT WriteCreationDate(ITableOutput& output) = 0;
    virtual HRESULT WriteLastModificationDate(ITableOutput& output) = 0;
    virtual HRESULT WriteLastAccessDate(ITableOutput& output) = 0;

    HRESULT WriteOwnerId(ITableOutput& output);
    HRESULT WriteOwnerSid(ITableOutput& output);
    HRESULT WriteOwner(ITableOutput& output);

    HRESULT WritePlatform(ITableOutput& output);
    HRESULT WriteTimeStamp(ITableOutput& output);
    HRESULT WriteSubSystem(ITableOutput& output);
    HRESULT WriteFileOS(ITableOutput& output);
    HRESULT WriteFileType(ITableOutput& output);
    HRESULT WriteVersion(ITableOutput& output);
    HRESULT WriteCompanyName(ITableOutput& output);
    HRESULT WriteProductName(ITableOutput& output);
    HRESULT WriteOriginalFileName(ITableOutput& output);

    HRESULT WritePeMD5(ITableOutput& output);
    HRESULT WritePeSHA1(ITableOutput& output);
    HRESULT WritePeSHA256(ITableOutput& output);

    HRESULT WriteMD5(ITableOutput& output);
    HRESULT WriteSHA1(ITableOutput& output);
    HRESULT WriteSHA256(ITableOutput& output);

    HRESULT WriteSSDeep(ITableOutput& output);
    HRESULT WriteTLSH(ITableOutput& output);

    HRESULT WriteSignedHash(ITableOutput& output);

    HRESULT WriteSecurityDirectory(ITableOutput& output);
    HRESULT WriteFirstBytes(ITableOutput& output);

    HRESULT WriteAuthenticodeStatus(ITableOutput& output);
    HRESULT WriteAuthenticodeSigner(ITableOutput& output);
    HRESULT WriteAuthenticodeSignerThumbprint(ITableOutput& output);
    HRESULT WriteAuthenticodeCA(ITableOutput& output);
    HRESULT WriteAuthenticodeCAThumbprint(ITableOutput& output);

protected:
    logger _L_;
    const std::shared_ptr<VolumeReader> m_pVolReader;

    HANDLE m_hFile;
    PEInfo m_PEInfo;

    std::wstring m_strComputerName;

    const WCHAR* m_szFullName;
    DWORD m_dwFullNameLen;

    bool m_bWriteErrorCodes;

    const std::vector<Filter>& m_Filters;
    Intentions m_DefaultIntentions;
    Intentions m_ColumnIntentions;

    Authenticode& m_codeVerifyTrust;

private:
    Intentions FilterIntentions(const std::vector<Filter>& Filters);
    bool FilterApplies(const Filter& filter);

    size_t FindVersionQueryValueRec(
        WCHAR* szValueName,
        size_t dwValueCchLength,
        LPBYTE ptr,
        LPBYTE ptr_end,
        int state,
        WCHAR** ppValue);
    HRESULT WriteVersionQueryValue(WCHAR* szValueName, ITableOutput& output);

    static const WCHAR* FileInfo::g_pszExecutableFileExtensions[];
    static const WCHAR* FileInfo::g_pszScriptFileExtensions[];
    static const WCHAR* FileInfo::g_pszArchiveFileExtensions[];
};

}  // namespace Orc

#pragma managed(pop)
