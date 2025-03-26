//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <memory>
#include <filesystem>
#include <system_error>

#include <boost/scope_exit.hpp>

#include "ArchiveExtract.h"

#include "Temporary.h"

#include "FileStream.h"
#include "CryptoHashStream.h"
#include "ResourceStream.h"

#include "EmbeddedResource.h"

#include "ZipExtract.h"

#include "ParameterCheck.h"

#include "SecurityDescriptor.h"

#include "WideAnsi.h"

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;

std::shared_ptr<ArchiveExtract> ArchiveExtract::MakeExtractor(ArchiveFormat fmt, bool bComputeHash)
{
    switch (fmt)
    {
        case ArchiveFormat::SevenZip:
            return std::make_shared<ZipExtract>(bComputeHash);
        case ArchiveFormat::Zip:
            return std::make_shared<ZipExtract>(bComputeHash);
    }
    return nullptr;
}

STDMETHODIMP ArchiveExtract::Extract(__in PCWSTR pwzZipFilePath, __in PCWSTR pwzExtractRootDir, __in PCWSTR szSDDL)
{
    std::vector<std::wstring> ListToExtract;

    return Extract(pwzZipFilePath, pwzExtractRootDir, szSDDL, ListToExtract);
}

STDMETHODIMP ArchiveExtract::Extract(
    __in PCWSTR pwzZipFilePath,
    __in PCWSTR pwzExtractRootDir,
    __in PCWSTR szSDDL,
    __in const std::vector<std::wstring>& ListToExtract)
{

    auto MakeArchiveStream = [pwzZipFilePath, szSDDL, this](std::shared_ptr<ByteStream>& stream) -> HRESULT {
        HRESULT hr = E_FAIL;

        if (EmbeddedResource::IsResourceBasedArchiveFile(pwzZipFilePath))
        {
            shared_ptr<ResourceStream> pResStream = make_shared<ResourceStream>();

            if (!pResStream)
            {
                return E_OUTOFMEMORY;
            }

            wstring strFileName(pwzZipFilePath);
            wstring ResName, MotherShip, NameInArchive, Format;

            if (FAILED(
                    hr = EmbeddedResource::SplitResourceReference(
                        strFileName, MotherShip, ResName, NameInArchive, Format)))
                return hr;

            HMODULE hMod = NULL;
            HRSRC hRes = NULL;
            std::wstring strBinaryPath;

            if (FAILED(
                    hr = EmbeddedResource::LocateResource(
                        MotherShip, ResName, EmbeddedResource::BINARY(), hMod, hRes, strBinaryPath)))
                return hr;

            if (FAILED(hr = pResStream->OpenForReadOnly(hMod, hRes)))
                return hr;

            stream = pResStream;
        }
        else
        {
            shared_ptr<FileStream> pFileStream = make_shared<FileStream>();

            if (!pFileStream)
                return E_OUTOFMEMORY;
            ;

            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = FALSE;
            sa.lpSecurityDescriptor = NULL;

            SecurityDescriptor sd;
            if (szSDDL != NULL)
            {
                if (SUCCEEDED(sd.ConvertFromSDDL(szSDDL)))
                    sa.lpSecurityDescriptor = sd.GetSecurityDescriptor();
            }

            if (FAILED(
                    hr = pFileStream->OpenFile(
                        pwzZipFilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        &sa,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL)))
                return hr;

            sd.FreeSecurityDescritor();

            stream = pFileStream;
        }
        return S_OK;
    };

    auto MakeWriteStream =
        [pwzExtractRootDir, szSDDL, this](OrcArchive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
        HRESULT hr = E_FAIL;

        std::wstring fileName;
        std::replace_copy(
            begin(item.NameInArchive), end(item.NameInArchive), std::back_insert_iterator(fileName), L'\\', L'_');
        std::replace(begin(fileName), end(fileName), L'/', L'_');

        HANDLE hFile = INVALID_HANDLE_VALUE;
        if (FAILED(
                hr = UtilGetUniquePath(
                    pwzExtractRootDir,
                    fileName.c_str(),
                    item.Path,
                    hFile,
                    FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                    szSDDL)))
        {
            Log::Error(
                L"Failed to get unique path for file '{}' in '{}' [{}]",
                item.NameInArchive,
                pwzExtractRootDir,
                SystemError(hr));
            return nullptr;
        }

        bool closeFileOnExit = true;
        BOOST_SCOPE_EXIT(&hFile, &closeFileOnExit)
        {
            if (closeFileOnExit && hFile != INVALID_HANDLE_VALUE)
                CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
        BOOST_SCOPE_EXIT_END;

        WCHAR szDirectory[ORC_MAX_PATH];

        if (FAILED(hr = GetDirectoryForFile(item.Path.c_str(), szDirectory, ORC_MAX_PATH)))
        {
            Log::Error(L"Failed to get directory for file '{}' [{}]", item.Path, SystemError(hr));
            return nullptr;
        }
        fs::path absolute(szDirectory);

        std::error_code ec;
        fs::create_directories(absolute, ec);
        if (ec)
        {
            Log::Error(L"Failed to create directory '{}' while processing file '{}' [{}]", absolute, item.Path, ec);
            return nullptr;
        }

        auto filestream = make_shared<FileStream>();
        if (FAILED(hr = filestream->OpenHandle(hFile)))
            return nullptr;

        m_ExtractedItems.push_back(std::make_pair(item.NameInArchive, item.Path));

        closeFileOnExit = false;

        return filestream;
    };

    auto ShouldItemBeExtracted = [&ListToExtract](const std::wstring& strNameInArchive) -> bool {
        // Test if we wanna skip some files
        if (ListToExtract.size() > 0)
        {
            bool bFound = false;
            for (const auto& spec : ListToExtract)
            {
                if (PathMatchSpec(strNameInArchive.c_str(), spec.c_str()))
                {
                    bFound = true;
                    break;
                }
            }
            if (bFound)
                return true;
            else
                return false;
        }
        return true;
    };
    return Extract(MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream);
}

STDMETHODIMP ArchiveExtract::Extract(
    __in const std::shared_ptr<ByteStream>& pArchiveStream,
    __in PCWSTR pwzExtractRootDir,
    __in PCWSTR szSDDL)
{

    auto MakeArchiveStream = [pArchiveStream, this](std::shared_ptr<ByteStream>& stream) -> HRESULT {
        HRESULT hr = E_FAIL;

        std::shared_ptr<FileStream> fs = std::dynamic_pointer_cast<FileStream>(pArchiveStream);

        if (fs)
        {
            auto newfs = std::make_shared<FileStream>();

            if (FAILED(hr = newfs->Duplicate(*fs)))
                return hr;

            stream = newfs;

            return S_OK;
        }

        std::shared_ptr<MemoryStream> ms = std::dynamic_pointer_cast<MemoryStream>(pArchiveStream);

        if (ms)
        {
            auto newms = std::make_shared<MemoryStream>();

            if (FAILED(hr = newms->Duplicate(*ms)))
                return hr;

            stream = newms;

            return S_OK;
        }

        return S_OK;
    };

    auto MakeWriteStream =
        [pwzExtractRootDir, szSDDL, this](OrcArchive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
        HRESULT hr = E_FAIL;

        HANDLE hFile = INVALID_HANDLE_VALUE;

        if (FAILED(
                hr = UtilGetUniquePath(
                    pwzExtractRootDir,
                    item.NameInArchive.c_str(),
                    item.Path,
                    hFile,
                    FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                    szSDDL)))
        {
            Log::Error(
                L"Failed to get unique path for file '{}' in '{}' [{}]",
                item.NameInArchive,
                pwzExtractRootDir,
                SystemError(hr));
            return nullptr;
        }

        WCHAR szDirectory[ORC_MAX_PATH];

        if (FAILED(hr = GetDirectoryForFile(item.Path.c_str(), szDirectory, ORC_MAX_PATH)))
        {
            Log::Error(L"Failed to get directory for file '{}' [{}]", item.Path, SystemError(hr));
            return nullptr;
        }
        fs::path absolute(szDirectory);

        std::error_code ec;
        fs::create_directories(absolute, ec);
        if (ec)
        {
            Log::Error(L"Failed to create directory '{}' [{}]", absolute, ec);
            return nullptr;
        }

        auto filestream = make_shared<FileStream>();
        if (FAILED(hr = filestream->OpenHandle(hFile)))
            return nullptr;

        m_ExtractedItems.push_back(std::make_pair(item.NameInArchive, item.Path));

        return filestream;
    };

    auto ShouldItemBeExtracted = [](const std::wstring&) -> bool { return true; };

    return Extract(MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream);
}

STDMETHODIMP ArchiveExtract::Extract(
    __in const std::shared_ptr<ByteStream>& pArchiveStream,
    __in PCWSTR pwzExtractRootDir,
    __in PCWSTR szSDDL,
    __in const std::vector<std::wstring>& ListToExtract)
{

    auto MakeArchiveStream = [pArchiveStream](std::shared_ptr<ByteStream>& stream) -> HRESULT {
        stream = pArchiveStream;
        return S_OK;
    };

    auto MakeWriteStream =
        [pwzExtractRootDir, szSDDL, this](OrcArchive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
        HRESULT hr = E_FAIL;

        HANDLE hFile = INVALID_HANDLE_VALUE;

        if (FAILED(
                hr = UtilGetUniquePath(
                    pwzExtractRootDir,
                    item.NameInArchive.c_str(),
                    item.Path,
                    hFile,
                    FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                    szSDDL)))
        {
            Log::Error(
                L"Failed to get unique path for file '{}' in '{}' [{}]",
                item.NameInArchive,
                pwzExtractRootDir,
                SystemError(hr));
            return nullptr;
        }

        WCHAR szDirectory[ORC_MAX_PATH];

        if (FAILED(hr = GetDirectoryForFile(item.Path.c_str(), szDirectory, ORC_MAX_PATH)))
        {
            Log::Error(L"Failed to get directory for file '{}' [{}]", item.Path, SystemError(hr));
            return nullptr;
        }
        fs::path absolute(szDirectory);

        std::error_code ec;
        fs::create_directories(absolute, ec);
        if (ec)
        {
            Log::Error(L"Failed to create directory '{}' [{}]", absolute, ec);
            return nullptr;
        }

        auto filestream = make_shared<FileStream>();
        if (FAILED(hr = filestream->OpenHandle(hFile)))
            return nullptr;

        m_ExtractedItems.push_back(std::make_pair(item.NameInArchive, item.Path));
        return filestream;
    };

    auto ShouldItemBeExtracted = [&ListToExtract](const std::wstring& strNameInArchive) -> bool {
        // Test if we wanna skip some files
        if (ListToExtract.size() > 0)
        {
            bool bFound = false;
            for (const auto& spec : ListToExtract)
            {
                if (PathMatchSpec(strNameInArchive.c_str(), spec.c_str()))
                {
                    bFound = true;
                    break;
                }
            }
            if (bFound)
                return false;
            else
                return true;
        }
        return true;
    };

    return Extract(MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream);
}

ArchiveExtract::~ArchiveExtract(void) {}
