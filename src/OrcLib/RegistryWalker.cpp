//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#include "stdafx.h"

#include "RegistryWalker.h"

using namespace Orc;

RegistryValue::RegistryValue(
    std::string&& ValueName,
    const RegistryKey* const ParentKey,
    ValueType Type,
    const BYTE* const pDatas,
    DWORD DatasLen,
    WORD wFlags,
    bool bDataIsResident)
    : m_ParentKey(ParentKey)
    , m_Type(Type)
    , m_pDatas(pDatas)
    , m_DatasLen(DatasLen)
    , m_wFlags(wFlags)
    , m_bDataIsResident(bDataIsResident)
{
    m_strValueName = std::move(ValueName);
}

ValueType RegistryValue::GetType() const
{
    return m_Type;
}

const std::string& RegistryValue::GetValueName() const
{
    return m_strValueName;
}

size_t RegistryValue::GetDatas(const BYTE** const pDatas) const
{
    *pDatas = m_pDatas;
    return m_DatasLen;
}

const RegistryKey* RegistryValue::GetParentKey() const
{
    return m_ParentKey;
}

const std::string& RegistryKey::GetKeyClassName() const
{
    return m_strClassName;
}

const FILETIME RegistryKey::GetKeyLastModificationTime() const
{
    return m_LastModificationTime;
}

bool RegistryValue::IsDataResident() const
{
    return m_bDataIsResident;
}

KeyType RegistryKey::GetKeyType() const
{
    return m_Type;
}

RegistryKey::RegistryKey(
    std::string&& KeyName,
    std::string&& ShortKeyName,
    std::string&& ClassName,
    const KeyHeader* const pRegKey,
    RegistryKey* const ParentKey,
    DWORD SubKeysCount,
    DWORD ValuesCount,
    FILETIME LastModificationTime,
    KeyType Type,
    bool bSukeyListIsResident,
    bool bValueListIsresident,
    bool bSkHeaderIsResident,
    bool bHasClassName)
    : m_ParentKey(ParentKey)
    , m_dwSubKeysCount(SubKeysCount)
    , m_dwValuesCount(ValuesCount)
    , m_Type(Type)
    , m_pRegKey(pRegKey)
    , m_bTreated(false)
    , m_dwSubKeysSeen(0)
    , m_bSubkeyListIsResident(bSukeyListIsResident)
    , m_bHasNonResidentSubkeys(false)
    , m_bValueListIsResident(bValueListIsresident)
    , m_bHasNonResidentValues(false)
    , m_bSkHeaderIsResident(true)
    , m_bHasClassName(bHasClassName)
{
    DBG_UNREFERENCED_PARAMETER(bSkHeaderIsResident);
    m_LastModificationTime = LastModificationTime;
    m_strKeyName = std::move(KeyName);
    m_strShortKeyName = std::move(ShortKeyName);
    m_strClassName = std::move(ClassName);
}
const std::string& RegistryKey::GetKeyName() const
{
    return m_strKeyName;
}

const std::string& RegistryKey::GetShortKeyName() const
{
    return m_strShortKeyName;
}

DWORD RegistryKey::GetSeenSubKeysCount() const
{
    return m_dwSubKeysSeen;
}

DWORD RegistryKey::GetSubKeysCount() const
{
    return m_dwSubKeysCount;
}

DWORD RegistryKey::GetValuesCount() const
{
    return m_dwValuesCount;
}

const RegistryKey* RegistryKey::GetParentKey() const
{
    return m_ParentKey;
}

RegistryKey* RegistryKey::GetAlterableParentKey()
{
    return m_ParentKey;
}

void RegistryKey::GetKeyResidencyState(
    bool* bSubkeyListIsresident,
    bool* bHasNonResidentSubkeys,
    bool* bValueListIsResident,
    bool* bHasNonResidentValues,
    bool* bSkHeaderIsResident) const
{
    if (bSubkeyListIsresident != nullptr)
        *bSubkeyListIsresident = m_bSubkeyListIsResident;
    if (bHasNonResidentSubkeys != nullptr)
        *bHasNonResidentSubkeys = m_bHasNonResidentSubkeys;
    if (bValueListIsResident != nullptr)
        *bValueListIsResident = m_bValueListIsResident;
    if (bHasNonResidentValues != nullptr)
        *bHasNonResidentValues = m_bHasNonResidentValues;
    if (bSkHeaderIsResident != nullptr)
        *bSkHeaderIsResident = m_bSkHeaderIsResident;
}

void RegistryKey::GetNamesState(bool* bHasName, bool* bHasClassName) const
{
    if (bHasName != nullptr)
        *bHasName = m_bHasName;
    if (bHasClassName != nullptr)
        *bHasClassName = m_bHasClassName;
}

void RegistryKey::SetHasNonResidentValues()
{
    m_bHasNonResidentValues = true;
}
void RegistryKey::SetHasNonResidentSubkeys()
{
    m_bHasNonResidentSubkeys = true;
}

void RegistryKey::IncrementSubKeysSeenCount(void)
{
    m_dwSubKeysSeen += 1;
}

const KeyHeader* RegistryKey::GetKeyHeader() const
{
    return m_pRegKey;
}

bool RegistryKey::GetKeyStatus() const
{
    return m_bTreated;
}

HRESULT RegistryKey::SetAsTreated()
{
    m_bTreated = true;
    return S_OK;
}

RegistryHive::RegistryHive(const std::wstring& HiveName)
    : m_strHiveName(HiveName)
    , m_bIsComplete(true)
{
    m_pHiveBuffer = nullptr;
    m_ulHiveBufferSize = 0;
    m_pLastModificationTime = nullptr;
    m_dwDataBlockSize = 0;
    m_dwRootKeyOffset = 0;
}

RegistryHive::RegistryHive()
    : m_bIsComplete(true)
{
    m_pHiveBuffer = nullptr;
    m_ulHiveBufferSize = 0;
    m_pLastModificationTime = nullptr;
    m_dwDataBlockSize = 0;
    m_dwRootKeyOffset = 0;
}

void RegistryHive::SetHiveIsNotComplete()
{
    m_bIsComplete = false;
}

bool RegistryHive::IsHiveComplete() const
{
    return m_bIsComplete;
}

HRESULT RegistryHive::LoadHive(ByteStream& HiveStream)
{

    HRESULT hr = E_FAIL;

    if ((hr = HiveStream.IsOpen()) != S_OK)
    {
        Log::Error("Hive stream seems to be closed");
        return hr;
    }
    if ((hr = HiveStream.CanRead()) != S_OK)
    {
        Log::Error("Can't read hive stream");
        return hr;
    }

    ULONG64 ulSize = HiveStream.GetSize();
    if (ulSize == 0)
    {
        Log::Error("Hive size is 0");
        return E_FAIL;
    }

    m_pHiveBuffer = (BYTE*)malloc((size_t)ulSize);
    if (m_pHiveBuffer == NULL)
    {
        Log::Error("Not enough memory to read hive");
        return E_OUTOFMEMORY;
    }
    m_ulHiveBufferSize = ulSize;

    ULONG64 ulRead = 0LL;
    ULONG64 ulTmp = 0LL;
    while (ulRead != ulSize)
    {

        hr = HiveStream.Read((PVOID)(m_pHiveBuffer + ulRead), ulSize - ulRead, &ulTmp);

        if (ulTmp == 0)
        {
            Log::Error("Read error, aborting read operation");
            free(m_pHiveBuffer);
            m_pHiveBuffer = nullptr;
            m_ulHiveBufferSize = 0LL;
            return hr;
        }
        ulRead += ulTmp;
    }
    if ((hr = ParseHiveHeader()) != S_OK)
    {
        free(m_pHiveBuffer);
        m_pHiveBuffer = nullptr;
        m_ulHiveBufferSize = 0L;
        Log::Error("Error during hive header parsing [{}]", SystemError(hr));
        return hr;
    }

    if ((hr = ParseHBinHeader()) != S_OK)
    {
        free(m_pHiveBuffer);
        m_pHiveBuffer = nullptr;
        m_ulHiveBufferSize = 0;
        Log::Error("Error during hive hbin header parsing [{}]", SystemError(hr));
        return hr;
    }
    return S_OK;
}

HRESULT RegistryHive::CheckBlockHeader(const BlockHeader* const pBlockHeader) const
{
    HRESULT hr = E_FAIL;
    if (pBlockHeader == nullptr)
    {
        Log::Error("Null pointer passed as parameter to CheckBlockHeader()");
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    if (-(int)(pBlockHeader->BlockSize) < 0)
    {
        Log::Error("BlockSize is negative (should be positive)");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if ((UINT_PTR)(pBlockHeader) + (size_t)(-(int)pBlockHeader->BlockSize)
        > m_ulHiveBufferSize + (UINT_PTR)m_pHiveBuffer)
    {
        Log::Error("Block end is outside of hive buffer boundary");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

HRESULT RegistryHive::CheckVkHeader(const ValueHeader* const pHeader, bool* bValueDataIsResident) const
{
    HRESULT hr = E_FAIL;

    if (pHeader == nullptr)
    {
        Log::Error("Null pointer passed as parameter to CheckVkHeader()");
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    if ((hr = CheckBlockHeader(&pHeader->Header)) != S_OK)
    {
        return hr;
    }

    if (_strnicmp(pHeader->Signature, "vk", 2))
    {
        Log::Error("Vk-block signature mismatch");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if ((sizeof(ValueHeader) + pHeader->NameLength) > (size_t)(4 - (int)pHeader->Header.BlockSize))
    {
        Log::Error("Vk-block size and computed size does not match");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    *bValueDataIsResident = true;
    /* If the sign bit (31st bit) in the length field is set, the value is
     * stored inline this struct, and not in a seperate data chunk */
    if (!(pHeader->DataLength & 0x80000000))
    {
        if ((pHeader->DataLength == 0) && (pHeader->OffsetToData != 0xFFFFFFFF))
        {
            Log::Error("Vk header: data offset/length incoherence");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        else if (!IsOffsetValid(pHeader->OffsetToData))
        {
            Log::Debug("Vk header: offset to data is invalid. Either vk header is corrupt or data is not resident");
            *bValueDataIsResident = false;
        }
    }

    if (pHeader->Type >= ValueType::RegMaxType)
    {
        Log::Debug("Value type is unknown");
    }

    return S_OK;
}

HRESULT RegistryHive::CheckLfHeader(const LF_LH_Header* const pHeader) const
{
    HRESULT hr = E_FAIL;

    if (pHeader == nullptr)
    {
        Log::Error("Null pointer passed as parameter to CheckLfHeader()");
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    if ((hr = CheckBlockHeader(&pHeader->Header)) != S_OK)
    {
        return hr;
    }

    if (_strnicmp(pHeader->Signature, "lf", 2) && _strnicmp(pHeader->Signature, "lh", 2))
    {
        Log::Error("Lf/Lh-block signature mismatch");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if ((sizeof(LF_LH_Header) + (pHeader->NumberOfKeys - 1) * sizeof(HashRecord))
        > (size_t)((4 - (int)pHeader->Header.BlockSize)))
    {
        Log::Error("Lf/Lh-block size and computed size does not match");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    return S_OK;
}

HRESULT RegistryHive::CheckNkHeader(
    const KeyHeader* const pHeader,
    bool* bSubkeyListIsResident,
    bool* bValueListIsResident,
    bool* bSkHeaderIsResident,
    bool* bHasClassName) const
{
    HRESULT hr = S_OK;

    if (pHeader == nullptr)
    {
        Log::Error("Null pointer passed as parameter to CheckNkHeader()");
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    if ((hr = CheckBlockHeader(&pHeader->Header)) != S_OK)
    {
        return hr;
    }

    if (_strnicmp(pHeader->Signature, "nk", 2))
    {
        Log::Error("Nk-block signature mismatch");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if ((sizeof(KeyHeader) + pHeader->NameLength) > (size_t)(4 - (int)pHeader->Header.BlockSize))
    {
        Log::Error("Nk-block size and computed size does not match");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    *bHasClassName = true;
    if ((pHeader->ClassNameLength == 0) && (pHeader->OffsetToClassName != 0xFFFFFFFF))
    {
        Log::Debug("Nk header: Class name incoherence");
        *bHasClassName = false;
    }
    else if (!IsOffsetValid(pHeader->OffsetToClassName))
    {
        Log::Debug("Nk header: Class name offset is invalid");
        *bHasClassName = false;
    }

    *bSubkeyListIsResident = true;
    if ((pHeader->NumberOfSubKeys == 0) && (pHeader->OffsetToLFHeader != 0xFFFFFFFF))
    {
        Log::Debug("Nk header: number of subkeys incoherence");
    }
    else if (!IsOffsetValid(pHeader->OffsetToLFHeader))
    {
        Log::Debug(
            "Nk header: subkey list offset is invalid. Either nk header is corrupt or subkey list is not resident");
        *bSubkeyListIsResident = false;
    }

    // check for all keys execpt root key!
    if (pHeader->Type == KeyType::key)
    {
        // Don't throw error because this field is never used to build keys. (Parent key is always known)
        if (!IsOffsetValid(pHeader->OffsetToParent))
        {
            Log::Debug("Nk header: ParentKey offset is invalid");
        }

        // check parent key signature

        if (_strnicmp(((KeyHeader*)FixOffset(pHeader->OffsetToParent))->Signature, "nk", 2))
        {
            Log::Debug("Nk header: ParentKey signature mismatch");
        }
    }
    else if ((pHeader->Type != KeyType::rootkey) && (pHeader->Type != KeyType::rootkeyAlternate))
    {
        Log::Debug("Key type is unknown");
    }

    if (!IsOffsetValid(pHeader->OffsetToSKHeader))
    {
        Log::Debug("Nk header: sk-header offset is invalid. Either nk header is corrupt or sk-header is not resident");
        *bSkHeaderIsResident = false;
    }

    *bValueListIsResident = true;
    if ((pHeader->NumberOfValues == 0) && (pHeader->OffsetToValueList != 0xFFFFFFFF))
    {
        Log::Debug("Number of values incoherence");
    }
    else if (!IsOffsetValid(pHeader->OffsetToValueList))
    {
        Log::Debug(
            "Nk header: value list offset is invalid. Either nk header is corrupt or value list is not resident");
        *bValueListIsResident = false;
    }

    return hr;
}

HRESULT RegistryHive::CheckHashRecord(const HashRecord* const pHeader, bool* bSubkeyIsResident) const
{
    HRESULT hr = E_FAIL;
    if (pHeader == nullptr)
    {
        Log::Error("Null pointer passed as parameter to CheckHashRecord()");
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    *bSubkeyIsResident = true;
    if (!IsOffsetValid(pHeader->OffsetToKeyHeader))
    {
        Log::Debug("HashRecord: subkey offset is invalid. Either hashrecord is corrupt or subkey is not resident");
        *bSubkeyIsResident = false;
    }

    return S_OK;
}

HRESULT RegistryHive::CheckLiHeader(const LI_RI_Header* const pHeader) const
{
    HRESULT hr = E_FAIL;
    if (pHeader == nullptr)
    {
        Log::Error("Null pointer passed as parameter to CheckLiHeader()");
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    if ((hr = CheckBlockHeader(&pHeader->Header)) != S_OK)
    {
        return hr;
    }

    if (_strnicmp(pHeader->Signature, "li", 2) && _strnicmp(pHeader->Signature, "ri", 2))
    {
        Log::Error("Li/ri-block signature mismatch");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if ((pHeader->NumberOfKeys - 1) * 4 + sizeof(LI_RI_Header) > (size_t)(4 - (int)pHeader->Header.BlockSize))
    {
        Log::Error("Li/ri-block size and computed size does not match ");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    return S_OK;
}

HRESULT RegistryHive::CheckDataHeader(const BlockHeader* const pHeader) const
{
    if (pHeader == nullptr)
    {
        Log::Error("Null pointer passed as parameter to CheckDataHeader()");
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    if ((UINT_PTR)(pHeader) + (size_t)(-(int)pHeader->BlockSize) > (UINT_PTR)(m_pHiveBuffer) + m_ulHiveBufferSize)
    {
        Log::Error("Data block end is outside of hive buffer boundary");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

HRESULT RegistryHive::ParseValues(
    RegistryKey* const pCurrentKey,
    std::function<void(const RegistryValue* const)> RegistryValueCallback)
{
    DWORD dwCount;
    HRESULT hr;
    bool bIsDataResident;
    bool bValueListIsResident;
    bIsDataResident = true;
    bValueListIsResident = true;

    if (pCurrentKey == nullptr)
    {
        return S_OK;
    }

    pCurrentKey->GetKeyResidencyState(nullptr, nullptr, &bValueListIsResident, nullptr, nullptr);

    if (((dwCount = pCurrentKey->GetValuesCount()) <= 0) || (bValueListIsResident == false))
    {
        return S_OK;
    }

    const KeyHeader* const pCurrentKeyHeader = pCurrentKey->GetKeyHeader();
    ValuesArray* pValuesList = (ValuesArray*)FixOffset(pCurrentKeyHeader->OffsetToValueList);
    DWORD i;
    const ValueHeader* pCurrentValue;
    for (i = 0; i < dwCount; i++)
    {

        if (!IsOffsetValid(pValuesList->ValueOffsets[i]))
        {
            Log::Debug("Key '{}': value offset in values list is invalid", pCurrentKey->GetKeyName());
            pCurrentKey->SetHasNonResidentValues();
            continue;
        }

        pCurrentValue = (ValueHeader*)FixOffset(pValuesList->ValueOffsets[i]);

        if ((hr = CheckVkHeader(pCurrentValue, &bIsDataResident)) == S_OK)
        {
            WORD wNameLen, wFlag;
            ValueType Type;
            const DataHeader* pDataHeader;
            DWORD dwDatasLen;

            wNameLen = pCurrentValue->NameLength;
            std::string Name(pCurrentValue->Name, wNameLen);

            BYTE* pData = nullptr;
            dwDatasLen = pCurrentValue->DataLength;
            if (bIsDataResident)
            {
                // Check for in place datas
                if (pCurrentValue->DataLength & 0x80000000)
                {
                    pData = (BYTE*)&(pCurrentValue->OffsetToData);
                    dwDatasLen = pCurrentValue->DataLength ^ 0x80000000;
                }
                // Datas is too large to fit, using an offset
                else
                {
                    pDataHeader = (DataHeader*)FixOffset(pCurrentValue->OffsetToData);
                    if ((hr = CheckDataHeader(&pDataHeader->Header)) != S_OK)
                    {
                        Log::Debug("Key '{}': Value data header is invalid", pCurrentKey->GetKeyName());
                        continue;
                    }
                    pData = (BYTE*)&(pDataHeader->Data);
                    if (dwDatasLen != (DWORD)(-(int)pDataHeader->Header.BlockSize))
                    {
                        Log::Debug(
                            "Keys '{}': size mismatch for data inside value '{}'", pCurrentKey->GetKeyName(), Name);
                    }
                }
            }

            wFlag = pCurrentValue->Flag;
            Type = pCurrentValue->Type;

            const RegistryValue* const CurrentRegValue =
                new RegistryValue(std::move(Name), pCurrentKey, Type, pData, dwDatasLen, wFlag, bIsDataResident);

            RegistryValueCallback(CurrentRegValue);

            delete CurrentRegValue;
        }
        else
        {
            Log::Debug("Key '{}': Vk structure is invalid", pCurrentKey->GetKeyName());
            continue;
        }
    }
    return S_OK;
}

HRESULT RegistryHive::ParseLfLh(
    LF_LH_Header* const pLfLhHeader,
    std::vector<RegistryKey*>& CurrentKeySet,
    RegistryKey* const CurrentKey,
    DWORD* pSubKeyCount)
{
    HRESULT hr = S_OK;
    DWORD dwCount;
    bool bSubkeyListIsResident;
    bool bCurrentSubKeySubkeyListIsResident;
    bool bCurrentSubKeyValueListIsResident;
    bool bSubkeyIsResident;
    bool bSkHeaderIsResident;
    bool bHasClassName;

    bCurrentSubKeySubkeyListIsResident = true;
    bCurrentSubKeyValueListIsResident = true;
    bSubkeyIsResident = true;
    bSubkeyListIsResident = true;
    bSkHeaderIsResident = true;

    if ((hr = CheckLfHeader(pLfLhHeader)) != S_OK)
    {
        Log::Error("Key '{}': lf/lh header is invalid", CurrentKey->GetKeyName());
        return hr;
    }

    // Check if there is subkeys to process
    CurrentKey->GetKeyResidencyState(&bSubkeyListIsResident, nullptr, nullptr, nullptr, nullptr);
    dwCount = pLfLhHeader->NumberOfKeys;
    if ((dwCount == 0) || (bSubkeyListIsResident == false))
    {  // no subkeys, call KeyCallback with current key
        return S_OK;
    }

    HashRecord* pHashRecords = pLfLhHeader->Records;
    DWORD i;
    i = 0;

    for (i = 0; i < dwCount; i++)
    {
        HashRecord pCurrentHashRecord = pHashRecords[i];

        *pSubKeyCount += 1;

        if ((hr = CheckHashRecord(&pCurrentHashRecord, &bSubkeyIsResident)) != S_OK)
        {
            continue;
        }

        // Key is resident
        if (bSubkeyIsResident)
        {
            KeyHeader* pCurrentSubKeyHeader = (KeyHeader*)FixOffset(pCurrentHashRecord.OffsetToKeyHeader);

            if ((hr = CheckNkHeader(
                     pCurrentSubKeyHeader,
                     &bCurrentSubKeySubkeyListIsResident,
                     &bCurrentSubKeyValueListIsResident,
                     &bSkHeaderIsResident,
                     &bHasClassName))
                != S_OK)
            {
                Log::Debug("Key '{}': lf/lh hash record point to an invalid nk header", CurrentKey->GetKeyName());
                continue;
            }

            std::string ClassName;
            if (bHasClassName)
            {
                ClassName.assign(
                    (CHAR*)FixOffset(pCurrentSubKeyHeader->OffsetToClassName), pCurrentSubKeyHeader->ClassNameLength);
            }
            else
            {
                ClassName.assign("NO CLASS NAME");
            }

            std::string Name;
            std::string ShortName(pCurrentSubKeyHeader->Name, pCurrentSubKeyHeader->NameLength);

            Name.reserve(CurrentKey->GetKeyName().length() + 1 + pCurrentSubKeyHeader->NameLength + 1);
            Name.append(CurrentKey->GetKeyName());
            Name.append("\\");
            Name.append(pCurrentSubKeyHeader->Name, pCurrentSubKeyHeader->NameLength);

            if (pCurrentSubKeyHeader->Type != KeyType::key)
            {
                Log::Debug(
                    "Key '{}': subkey with invalid key type (0x%x)",
                    CurrentKey->GetKeyName(),
                    static_cast<size_t>(pCurrentSubKeyHeader->Type));
            }

            RegistryKey* const RegistrySubKey = new RegistryKey(
                std::move(Name),
                std::move(ShortName),
                std::move(ClassName),
                pCurrentSubKeyHeader,
                CurrentKey,
                pCurrentSubKeyHeader->NumberOfSubKeys,
                pCurrentSubKeyHeader->NumberOfValues,
                pCurrentSubKeyHeader->LastModificationDate,
                pCurrentSubKeyHeader->Type,
                bCurrentSubKeySubkeyListIsResident,
                bCurrentSubKeyValueListIsResident,
                bSkHeaderIsResident,
                bHasClassName);

            // Add key to key set for further treatment
            CurrentKeySet.push_back(RegistrySubKey);
        }
        else
        {  // SubKey is or is not resident
            CurrentKey->SetHasNonResidentSubkeys();
            // MAY ADD SOMETHING IN FUTURE (got at lesat a hash/the 4th first chars of the key name...)
        }
    }

    return S_OK;
}

HRESULT RegistryHive::ParseLiRi(
    LI_RI_Header* const pRiLiHeader,
    std::vector<RegistryKey*>& CurrentKeySet,
    RegistryKey* const CurrentKey,
    DWORD* pSubKeyCount)
{
    HRESULT hr = S_OK;
    DWORD dwCount, i;

    bool bCurrentSubKeySubkeyListIsResident;
    bool bCurrentSubKeyValueListIsResident;
    bool bSubkeyIsResident;
    bool bSkHeaderIsResident;
    bool bHasClassName;

    bHasClassName = true;
    bCurrentSubKeySubkeyListIsResident = true;
    bCurrentSubKeyValueListIsResident = true;
    bSubkeyIsResident = true;
    bSkHeaderIsResident = true;

    bSubkeyIsResident = true;
    if ((hr = CheckLiHeader(pRiLiHeader)) != S_OK)
    {
        Log::Error("Key '{}': Li/ri header is invalid [{}]", CurrentKey->GetKeyName(), SystemError(hr));
        return hr;
    }

    dwCount = pRiLiHeader->NumberOfKeys;

    for (i = 0; i < dwCount; i++)
    {

        if (!IsOffsetValid(pRiLiHeader->Records[i].OffsetToKeyHeader))
        {
            Log::Debug(
                "Key '{}': Li/Ri record: offset to key is invalid. Either Li/Ri record is invalid or key is not "
                "resident",
                CurrentKey->GetKeyName());
            CurrentKey->SetHasNonResidentSubkeys();
            continue;
        }

        DataHeader* pHeader = (DataHeader*)FixOffset(pRiLiHeader->Records[i].OffsetToKeyHeader);

        // Check if subkey list is of type LF/LH
        if (!_strnicmp((CHAR*)pHeader->Data, "lh", 2) || !_strnicmp((CHAR*)pHeader->Data, "lf", 2))
        {
            ParseLfLh((LF_LH_Header* const)pHeader, CurrentKeySet, CurrentKey, pSubKeyCount);
        }
        // Check if subkey list is of type LI/RI
        else if (!_strnicmp((CHAR*)pHeader->Data, "li", 2) || !_strnicmp((CHAR*)pHeader->Data, "ri", 2))
        {
            hr = ParseLiRi((LI_RI_Header* const)pHeader, CurrentKeySet, CurrentKey, pSubKeyCount);
        }
        // Check if subkey list is of type NK
        else if (!_strnicmp((CHAR*)pHeader->Data, "nk", 2))
        {

            *pSubKeyCount += 1;

            KeyHeader* pCurrentSubKeyHeader = (KeyHeader*)pHeader;
            if ((hr = CheckNkHeader(
                     pCurrentSubKeyHeader,
                     &bCurrentSubKeySubkeyListIsResident,
                     &bCurrentSubKeyValueListIsResident,
                     &bSkHeaderIsResident,
                     &bHasClassName))
                != S_OK)
            {
                // continue if key is invalid
                continue;
            }

            // std::string Name(pCurrentSubKeyHeader->Name,pCurrentSubKeyHeader->NameLength);
            std::string Name;
            Name.reserve(CurrentKey->GetKeyName().length() + 1 + 1 + pCurrentSubKeyHeader->NameLength);
            Name.append(CurrentKey->GetKeyName());
            Name.append("\\");
            Name.append(pCurrentSubKeyHeader->Name, pCurrentSubKeyHeader->NameLength);

            std::string ShortName(pCurrentSubKeyHeader->Name, pCurrentSubKeyHeader->NameLength);

            std::string ClassName;
            if (bHasClassName)
            {
                ClassName.assign(
                    (CHAR*)FixOffset(pCurrentSubKeyHeader->OffsetToClassName), pCurrentSubKeyHeader->ClassNameLength);
            }
            else
            {
                ClassName.assign("NO CLASS NAME");
            }

            RegistryKey* const RegistrySubKey = new RegistryKey(
                std::move(Name),
                std::move(ShortName),
                std::move(ClassName),
                pCurrentSubKeyHeader,
                CurrentKey,
                pCurrentSubKeyHeader->NumberOfSubKeys,
                pCurrentSubKeyHeader->NumberOfValues,
                pCurrentSubKeyHeader->LastModificationDate,
                pCurrentSubKeyHeader->Type,
                bCurrentSubKeySubkeyListIsResident,
                bCurrentSubKeyValueListIsResident,
                bSkHeaderIsResident,
                bHasClassName);

            CurrentKeySet.push_back(RegistrySubKey);
        }
        // else error
        else
        {
            Log::Debug(
                "Key '{}': Unknown signature for subkeys list! Should be 'nk','lf','lh,'ri' or 'li' and is '{}{}'",
                CurrentKey->GetKeyName(),
                (char)pHeader->Data[0],
                (char)pHeader->Data[1]);
            continue;
        }
    }
    return S_OK;
}

HRESULT RegistryHive::ParseNks(RegistryKey* const CurrentKey, std::vector<RegistryKey*>& CurrentKeySet)
{
    HRESULT hr = S_OK;
    DWORD dwCount;

    dwCount = 0;

    const KeyHeader* const pCurrentKeyHeader = CurrentKey->GetKeyHeader();
    bool bSubKeyListIsResident;
    bSubKeyListIsResident = true;

    CurrentKey->GetKeyResidencyState(&bSubKeyListIsResident, nullptr, nullptr, nullptr, nullptr);

    if ((pCurrentKeyHeader->NumberOfSubKeys == 0) || (bSubKeyListIsResident == false))
    {
        return S_OK;
    }
    DataHeader* pHeader;

    pHeader = (DataHeader*)FixOffset(pCurrentKeyHeader->OffsetToLFHeader);

    if (!_strnicmp((CHAR*)pHeader->Data, "lh", 2) || !_strnicmp((CHAR*)pHeader->Data, "lf", 2))
    {
        hr = ParseLfLh((LF_LH_Header* const)pHeader, CurrentKeySet, CurrentKey, &dwCount);
    }
    else if (!_strnicmp((CHAR*)pHeader->Data, "li", 2) || !_strnicmp((CHAR*)pHeader->Data, "ri", 2))
    {
        hr = ParseLiRi((LI_RI_Header* const)pHeader, CurrentKeySet, CurrentKey, &dwCount);
    }
    else
    {
        LPCSTR pHead = (CHAR*)pHeader;
        Log::Error(
            "Unknown signature for subkeys list! Should be 'lf','lh,'ri' or 'li' and is {:#x}{:x}", pHead[0], pHead[1]);
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }

    if (dwCount != pCurrentKeyHeader->NumberOfSubKeys)
    {
        Log::Debug(
            L"Invalid number of subkeys. Some subkeys might be non resident, if it's not the case, something went "
            L"wrong",
            dwCount,
            pCurrentKeyHeader->NumberOfSubKeys);
    }

    return hr;
}

HRESULT RegistryHive::ParseHiveHeader()
{
    // Hive are at least 0x1000 in size
    if (m_ulHiveBufferSize < 0x1000)
    {
        Log::Error("Hive invalid (too small)");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // Verify header
    RegistryFile* pRegFile;
    pRegFile = (RegistryFile*)m_pHiveBuffer;
    if (strncmp(pRegFile->Signature, "regf", 4))
    {
        Log::Error("Hive signature 'regf' mismatch");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if (pRegFile->Reserved1 != pRegFile->Reserved2)
    {
        Log::Debug("Some values of hive header are inconsistant (not fatal)");
    }

    if (pRegFile->DataBlockSize + 0x1000 >= m_ulHiveBufferSize)
    {
        SetHiveIsNotComplete();
        Log::Debug("Hive size mismatch. Either Hive is incomplete or some parts are not resident");
    }

    m_dwRootKeyOffset = pRegFile->OffsetToKeyRecord;
    if (!IsOffsetValid(m_dwRootKeyOffset))
    {
        m_dwRootKeyOffset = 0;
        Log::Error("Hive rootKey seems to be outside of hive");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    m_dwDataBlockSize = pRegFile->DataBlockSize;
    if (!IsOffsetValid(m_dwDataBlockSize))
    {
        SetHiveIsNotComplete();
        Log::Debug("Hive data block is bigger than hive. Either Hive is incomplete or some parts are not resident");
    }

    m_pLastModificationTime = &pRegFile->LastModificationDate;

    return S_OK;
}

HRESULT RegistryHive::ParseHBinHeader()
{

    HRESULT hr = E_FAIL;

    if (m_ulHiveBufferSize < 0x1004)
    {
        Log::Error("Hive invalid (too small)");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    HBINHeader* pHBinHeader = (HBINHeader*)(m_pHiveBuffer + 0x1000);
    if (strncmp(pHBinHeader->Signature, "hbin", 4))
    {
        Log::Error(L"Hive '{}': bad hbin header", m_strHiveName);
        return E_FAIL;
    }

    // TODO: check hbin chains consistancy
    return S_OK;
}

HRESULT RegistryHive::Walk(
    std::function<void(const RegistryKey* const)> RegistryKeyCallBack,
    std::function<void(const RegistryValue* const)> RegistryValueCallback)
{
    HRESULT hr = E_FAIL;

    bool bSubkeyListIsResident;
    bool bValueListIsResident;
    bool bSkHeaderIsResident;
    bool bHasClassName;

    bSubkeyListIsResident = true;
    bValueListIsResident = true;
    bSkHeaderIsResident = true;

    if (!m_dwDataBlockSize || !m_ulHiveBufferSize || !m_dwRootKeyOffset)
    {
        Log::Error(L"Hive '{}': hive is not loaded", m_strHiveName);
        return E_UNEXPECTED;
    }
    // Get Root key
    KeyHeader* pRegKey;
    pRegKey = (KeyHeader*)FixOffset(m_dwRootKeyOffset);

    if (CheckNkHeader(pRegKey, &bSubkeyListIsResident, &bValueListIsResident, &bSkHeaderIsResident, &bHasClassName)
        != S_OK)
    {
        Log::Error(L"Hive '{}': root key is invalid", m_strHiveName);
        return ERROR_INVALID_DATA;
    }

    if ((pRegKey->Type != KeyType::rootkey) && (pRegKey->Type != KeyType::rootkeyAlternate))
    {
        Log::Debug(L"Hive '{}': root key is not of type RootKey", m_strHiveName);
    }

    std::string ClassName;
    if (bHasClassName)
    {
        ClassName.assign((CHAR*)FixOffset(pRegKey->OffsetToClassName), pRegKey->ClassNameLength);
    }
    else
    {
        ClassName.assign("NO CLASS NAME");
    }

    // RootKey name is empty by convention
    std::string Name("");
    std::string ShortName(pRegKey->Name, pRegKey->NameLength);

    // Build rootkey
    RegistryKey* const RootKeyRegistryKey = new RegistryKey(
        std::move(Name),
        std::move(ShortName),
        std::move(ClassName),
        (const KeyHeader* const)FixOffset(m_dwRootKeyOffset),
        NULL,
        pRegKey->NumberOfSubKeys,
        pRegKey->NumberOfValues,
        pRegKey->LastModificationDate,
        pRegKey->Type,
        bSubkeyListIsResident,
        bValueListIsResident,
        bSkHeaderIsResident,
        bHasClassName);

    std::vector<RegistryKey*> CurrentKeySet;
    CurrentKeySet.push_back(RootKeyRegistryKey);
    RegistryKey* CurrentKey;
    while (!CurrentKeySet.empty())
    {
        CurrentKey = CurrentKeySet.back();

        // check if this key as already been treated
        if (CurrentKey->GetKeyStatus())
        {
            CurrentKeySet.pop_back();
            if (CurrentKey->GetSubKeysCount() != CurrentKey->GetSeenSubKeysCount())
                Log::Debug(
                    "Key '{}': number of subkeys parsed is different from number of subkeys announced.({} announced, "
                    "{} parsed)",
                    CurrentKey->GetKeyName(),
                    CurrentKey->GetSubKeysCount(),
                    CurrentKey->GetSeenSubKeysCount());

            delete CurrentKey;
            continue;
        }

        RegistryKey* const pParentKey = CurrentKey->GetAlterableParentKey();

        // Increment counter of treated subkeys
        if (pParentKey != nullptr)
            pParentKey->IncrementSubKeysSeenCount();

        if ((hr = ParseNks(CurrentKey, CurrentKeySet)) != S_OK)
        {
            Log::Debug("Error during parsing of '{}' subkeys", CurrentKey->GetKeyName());
        }
        if ((hr = ParseValues(CurrentKey, RegistryValueCallback)) != S_OK)
        {
            Log::Debug("Error during parsing of '{}' values", CurrentKey->GetKeyName());
        }
        // Set key as treated!

        CurrentKey->SetAsTreated();
        // call key callback
        RegistryKeyCallBack(CurrentKey);
    }
    return S_OK;
}
