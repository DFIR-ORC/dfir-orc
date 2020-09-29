//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#pragma once

#include <cstdio>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

#pragma pack(push)
#pragma pack(8)

//"regf" is obviosly the abbreviation for "Registry file". "regf" is the
// signature of the header-block which is always 4kb in size, although only
// the first 64 bytes seem to be used and a checksum is calculated over
// the first 0x200 bytes only!
//
struct RegistryFile
{
    // Offset		Size	Contents
    char Signature[4];  // 0x00000000	D-Word	ID: ASCII-"regf" = 0x66676572
    DWORD Reserved1;  // 0x00000004	D-Word	????
    DWORD Reserved2;  // 0x00000008	D-Word	???? Always the same value as at 0x00000004
    FILETIME LastModificationDate;  // 0x0000000C	Q-Word	last modify date in WinNT date-format
    DWORD Reserved_Always1;  // 0x00000014	D-Word	1
    DWORD Reserved_Always3;  // 0x00000018	D-Word	3
    DWORD Reserved_Always0;  // 0x0000001C	D-Word	0
    DWORD Reserved_Always1_;  // 0x00000020	D-Word	1
    DWORD OffsetToKeyRecord;  // 0x00000024	D-Word	Offset of 1st key record
    DWORD DataBlockSize;  // 0x00000028	D-Word	Size of the data-blocks (Filesize-4kb)
    DWORD Reserved_Always1__;  // 0x0000002C	D-Word	1
    DWORD CheckSum;  // 0x000001FC	D-Word	Sum of all D-Words from 0x00000000 to 0x000001FB
};

// the "hbin"-Block
//================
//
// I don't know what "hbin" stands for, but this block is always a multiple
// of 4kb in size.
//
// Inside these hbin-blocks the different records are placed. The memory-
// management looks like a C-compiler heap management to me...
// The values in 0x0008 and 0x001C should be the same, so I don't know
// if they are correct or swapped...
//
// From offset 0x0020 inside a hbin-block data is stored with the following format:
//
// Offset	Size	Contents
// 0x0000	D-Word	Data-block size
// 0x0004	????	Data
//
// If the size field is negative (bit 31 set), the corresponding block
// is free and has a size of -blocksize!
// The data is stored as one record per block. Block size is a multiple
// of 4 and the last block reaches the next hbin-block, leaving no room.

struct HBINHeader
{
    // Offset	Size	Contents
    char Signature[4];  // 0x0000	D-Word	ID: ASCII-"hbin" = 0x6E696268
    DWORD OffsetToFirst;  // 0x0004	D-Word	Offset from the 1st hbin-Block
    DWORD OffsetToNext;  // 0x0008	D-Word	Offset to the next hbin-Block
    DWORD BlockSize;  // 0x001C	D-Word	Block-size
};

struct BlockHeader
{
    DWORD BlockSize;
};

enum KeyType : WORD
{
    rootkey = 0x2C,
    rootkeyAlternate = 0xac,
    key = 0x20
};

struct KeyHeader
{

    BlockHeader Header;
    // Offset	Size	Contents
    char Signature[2];  // 0x0000	Word	ID: ASCII-"nk" = 0x6B6E
    KeyType Type;  // 0x0002	Word	for the root-key: 0x2C, otherwise 0x20
    FILETIME LastModificationDate;  // 0x0004	Q-Word	write-date/time in windows nt notation
    DWORD Reserved1;
    DWORD OffsetToParent;  // 0x0010	D-Word	Offset of Owner/Parent key
    DWORD NumberOfSubKeys;  // 0x0014	D-Word	number of sub-Keys
    DWORD Reserved2;
    DWORD OffsetToLFHeader;  // 0x001C	D-Word	Offset of the sub-key lf-Records
    DWORD Reserved3;
    DWORD NumberOfValues;  // 0x0024	D-Word	number of values
    DWORD OffsetToValueList;  // 0x0028	D-Word	Offset of the Value-List
    DWORD OffsetToSKHeader;  // 0x002C	D-Word	Offset of the sk-Record
    DWORD OffsetToClassName;  // 0x0030	D-Word	Offset of the Class-Name
    DWORD Reserverd4[5];  // 0x0044	D-Word	Unused (data-trash)
    WORD NameLength;  // 0x0048	Word	name-length
    WORD ClassNameLength;  // 0x004A	Word	class-name length
    CHAR Name[1];  // 0x004C	????	key-name
};

struct ValuesArray
{
    BlockHeader Header;
    DWORD ValueOffsets[1];
};

//#define REG_NONE                    ( 0 )   // No value type
//#define REG_SZ                      ( 1 )   // Unicode nul terminated string
//#define REG_EXPAND_SZ               ( 2 )   // Unicode nul terminated string
//                                            // (with environment variable references)
//#define REG_BINARY                  ( 3 )   // Free form binary
//#define REG_DWORD                   ( 4 )   // 32-bit number
//#define REG_DWORD_LITTLE_ENDIAN     ( 4 )   // 32-bit number (same as REG_DWORD)
//#define REG_DWORD_BIG_ENDIAN        ( 5 )   // 32-bit number
//#define REG_LINK                    ( 6 )   // Symbolic Link (unicode)
//#define REG_MULTI_SZ                ( 7 )   // Multiple Unicode strings
//#define REG_RESOURCE_LIST           ( 8 )   // Resource list in the resource map
//#define REG_FULL_RESOURCE_DESCRIPTOR ( 9 )  // Resource list in the hardware description
//#define REG_RESOURCE_REQUIREMENTS_LIST ( 10 )
//#define REG_QWORD                   ( 11 )  // 64-bit number
//#define REG_QWORD_LITTLE_ENDIAN     ( 11 )  // 64-bit number (same as REG_QWORD)

enum ValueType : DWORD
{
    // Value		  Type
    RegNone = 0x0000,
    RegSZ = 0x0001,  // RegSZ: 		character string (in UNICODE!)
    ExpandSZ = 0x0002,  // ExpandSZ: 	string with "%var%" expanding (UNICODE!)
    RegBin = 0x0003,  // RegBin:		raw-binary value
    RegDWORD = 0x0004,  // RegDWord:	Dword
    RegDWORDBE = 0x0005,  // RegDWORDBE: Big Endian DWORD
    RegLink = 0x0006,  // RegLink: Symbolic link
    RegMultiSZ = 0x0007,  // RegMultiSZ:	multiple strings, seperated with 0 (UNICODE!)
    RegResourceList = 0x0008,  // RegResourceList: Resource list in the resource map
    RegResourceDescr = 0x0009,  // RegResourceDescr: Resource list in the hardware description
    RegResourceReqList = 0x000A,  // RegResourceReqList:  Resource list in the hardware description
    RegQWORD = 0x000B,  // RegQWORD:  64-bit number
    RegMaxType = 0x000C  // First invalid type
};

struct DataHeader
{
    BlockHeader Header;
    BYTE Data[1];
};

struct ValueHeader
{
    BlockHeader Header;
    // Offset	Size	Contents
    char Signature[2];  // 0x0000	Word	ID: ASCII-"vk" = 0x6B76
    WORD NameLength;  // 0x0002	Word	name length
    DWORD DataLength;  // 0x0004	D-Word	length of the data
    DWORD OffsetToData;  // 0x0008	D-Word	Offset of Data
    ValueType Type;  // 0x000C	D-Word	Type of value
    WORD Flag;  // 0x0010	Word	Flag
    WORD Reserved;  // 0x0012	Word	Unused (data-trash)
    CHAR Name[1];  // 0x0014	????	Name
};

// Hash-Record
//===========
// Keep in mind, that the value at 0x0004 is used for checking the
// data-consistency! If you change the key-name you have to change the
// hash-value too!

struct HashRecord
{
    // Offset	Size	Contents
    DWORD OffsetToKeyHeader;  // 0x0000	D-Word	Offset of corresponding "nk"-Record
    CHAR FirstFour[4];  // 0x0004	D-Word	ASCII: the first 4 characters of the key-name,
                        //		padded with 0's. Case sensitiv!
};

// The "lf"|"lh" -record
//===============
struct LF_LH_Header
{
    BlockHeader Header;
    // Offset	Size	Contents
    CHAR Signature[2];  // 0x0000	Word	ID: ASCII-"lf" || "lh"
    WORD NumberOfKeys;  // 0x0002	Word	number of keys
    HashRecord Records[1];  // 0x0004	????	Hash-Records
};

// The "li"|"ri" -record
struct NoHashRecord
{
    // Offset	Size	Contents
    DWORD OffsetToKeyHeader;  // 0x0000	D-Word	Offset of corresponding "nk"-Record
};

struct LI_RI_Header
{
    BlockHeader Header;
    // Offset	Size	Contents
    CHAR Signature[2];  // 0x0000	Word	ID: ASCII-"li" || "ri"
    WORD NumberOfKeys;  // 0x0002	Word	number of keys
    NoHashRecord Records[1];  // 0x0004	????	Hash-Records
};

// The "sk"-block
//==============
//
//(due to the complexity of the SAM-info, not clear jet)
// The usage counter counts the number of references to this
//"sk"-record. You can use one "sk"-record for the entire registry!

struct SKHeader
{
    // Offset	Size	Contents
    CHAR Signature[2];  // 0x0000	Word	ID: ASCII-"sk" = 0x6B73
    WORD Reserved;  // 0x0002	Word	Unused
    DWORD OffsetToPrevious;  // 0x0004	D-Word	Offset of previous "sk"-Record
    DWORD OffsetToNext;  // 0x0008	D-Word	Offset of next "sk"-Record
    DWORD UsageCounter;  // 0x000C	D-Word	usage-counter
    DWORD Size;  // 0x0010	D-Word	Size of "sk"-record in bytes
                 //????
                 //????	????	Security and auditing settings...
                 //????
};

#pragma pack(pop)

class RegistryKey;

class ORCLIB_API RegistryValue
{
private:
    const BYTE* const m_pDatas;
    DWORD m_DatasLen;
    bool m_bDataIsResident;

    ValueType m_Type;
    WORD m_wFlags;
    std::string m_strValueName;
    const RegistryKey* const m_ParentKey;

public:
    RegistryValue(
        std::string&& ValueName,
        const RegistryKey* const ParentKey,
        ValueType Type,
        const BYTE* const Datas,
        DWORD DatasLen,
        WORD Flags,
        bool bDataIsResident);
    ValueType GetType() const;
    const std::string& GetValueName() const;
    size_t GetDatas(const BYTE** const pDatas) const;
    const RegistryKey* GetParentKey() const;
    bool IsDataResident() const;
};

class ORCLIB_API RegistryHive
{
private:
    BYTE* m_pHiveBuffer;
    ULONG64 m_ulHiveBufferSize;

    FILETIME* m_pLastModificationTime;
    DWORD m_dwRootKeyOffset;
    DWORD m_dwDataBlockSize;
    bool m_bIsComplete;

    std::wstring m_strHiveName;

    std::function<void(const RegistryKey&)> m_RegistryKeyCallBack;
    std::function<void(const RegistryValue&)> m_RegistryValueCallback;

    BYTE* FixOffset(DWORD offset) const { return (m_pHiveBuffer + (int)offset + 0x1000); };

    bool IsOffsetValid(DWORD dwOffset) const
    {
        return (dwOffset == 0xFFFFFFFF || (((int)dwOffset + 0x1000) <= (m_ulHiveBufferSize)) ? true : false);
    };

    HRESULT
    ParseValues(RegistryKey* const pRegistryKey, std::function<void(const RegistryValue* const)> RegistryValueCallback);
    HRESULT ParseNks(RegistryKey* const ParentKey, std::vector<RegistryKey*>& CurrentKeySet);
    HRESULT ParseLfLh(
        LF_LH_Header* const plfLhHeader,
        std::vector<RegistryKey*>& CurrentKeySet,
        RegistryKey* const ParentKey,
        DWORD* pSubKeyCount);
    HRESULT ParseLiRi(
        LI_RI_Header* const plfLhHeader,
        std::vector<RegistryKey*>& CurrentKeySet,
        RegistryKey* const ParentKey,
        DWORD* pSubKeyCount);

    HRESULT ParseHiveHeader();
    HRESULT ParseHBinHeader();

    HRESULT CheckBlockHeader(const BlockHeader* const pDataHeader) const;

    HRESULT CheckVkHeader(const ValueHeader* const pValueHeader, bool* bValueListIsResident) const;
    HRESULT CheckLfHeader(const LF_LH_Header* const pLfHeader) const;
    HRESULT CheckNkHeader(
        const KeyHeader* const pHeader,
        bool* bSubkeyListIsResident,
        bool* bValueListIsResident,
        bool* bSkHeaderIsResident,
        bool* bHasClassName) const;
    HRESULT CheckHashRecord(const HashRecord* const pHeader, bool* bSubkeyIsResident) const;
    HRESULT CheckLiHeader(const LI_RI_Header* const pHeader) const;

    HRESULT CheckDataHeader(const BlockHeader* const pHeader) const;

    void SetHiveIsNotComplete();

public:
    RegistryHive(const std::wstring& HiveName);
    RegistryHive();

    HRESULT LoadHive(ByteStream& HiveStream);
    HRESULT Walk(
        std::function<void(const RegistryKey* const)> RegistryKeyCallBack,
        std::function<void(const RegistryValue* const)> RegistryValueCallback);
    bool IsHiveComplete() const;

    ~RegistryHive()
    {
        if (m_pHiveBuffer != nullptr)
            free(m_pHiveBuffer);
    };
};

class ORCLIB_API RegistryKey
{

    friend HRESULT RegistryHive::Walk(
        std::function<void(const RegistryKey* const)> RegistryKeyCallBack,
        std::function<void(const RegistryValue* const)> RegistryValueCallback);

private:
    RegistryKey* GetAlterableParentKey();

    std::string m_strKeyName;
    std::string m_strShortKeyName;
    std::string m_strClassName;
    DWORD m_dwSubKeysCount;  // Hold number of subkeys when key callback is called
    DWORD m_dwSubKeysSeen;
    DWORD m_dwValuesCount;  // Hold number of values when value callback is called

    FILETIME m_LastModificationTime;
    KeyType m_Type;

    RegistryKey* const m_ParentKey;  // ptr needed, can be null (root key! lost keys?)

    const KeyHeader* const m_pRegKey;
    bool m_bTreated;

    // Non resident/incomplete keys

    bool m_bSubkeyListIsResident;
    bool m_bHasNonResidentSubkeys;
    bool m_bValueListIsResident;
    bool m_bHasNonResidentValues;
    bool m_bSkHeaderIsResident;
    bool m_bHasClassName;
    bool m_bHasName;

public:
    void SetHasNonResidentValues();
    void SetHasNonResidentSubkeys();
    // Functions used during walking only
    void IncrementSubKeysSeenCount();
    DWORD GetSeenSubKeysCount() const;
    const KeyHeader* GetKeyHeader() const;

    bool GetKeyStatus() const;
    HRESULT SetAsTreated();

    const RegistryKey* GetParentKey() const;
    const std::string& GetKeyName() const;
    const std::string& GetShortKeyName() const;
    const std::string& GetKeyClassName() const;
    DWORD GetSubKeysCount() const;
    DWORD GetValuesCount() const;
    KeyType GetKeyType() const;
    const FILETIME GetKeyLastModificationTime() const;
    void GetKeyResidencyState(
        bool* bSubkeyListIsresident,
        bool* bHasNonResidentSubkeys,
        bool* bValueListIsResident,
        bool* bHasNonResidentValues,
        bool* bSkHeaderIsResident) const;
    void GetNamesState(bool* bHasName, bool* bHasClassName) const;

    RegistryKey(
        std::string&& KeyName,
        std::string&& ShortKeyName,
        std::string&& ClassName,
        const KeyHeader* const m_pRegKey,
        RegistryKey* const Parent,
        DWORD SubKeysCount,
        DWORD ValuesCount,
        FILETIME LastModificationTime,
        KeyType Type,
        bool bSubkeyListIsResident,
        bool bValueListIsResident,
        bool bSkHeaderIsResident,
        bool bHasClassName);
};

}  // namespace Orc

#pragma managed(pop)
