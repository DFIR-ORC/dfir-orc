//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
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

#include "LogFileWriter.h"

#include "FileStream.h"
#include "XORStream.h"
#include "CryptoHashStream.h"
#include "ResourceStream.h"

#include "EmbeddedResource.h"

#include "ZipExtract.h"
#include "CabExtract.h"

#include "ParameterCheck.h"

#include "SecurityDescriptor.h"

using namespace std;
namespace fs = std::experimental::filesystem;

using namespace Orc;

std::shared_ptr<ArchiveExtract> ArchiveExtract::MakeExtractor(ArchiveFormat fmt, logger pLog, bool bComputeHash)
{
    switch (fmt)
    {
        case ArchiveFormat::Cabinet:
            return std::make_shared<CabExtract>(std::move(pLog), bComputeHash);
        case ArchiveFormat::SevenZip:
            return std::make_shared<ZipExtract>(std::move(pLog), bComputeHash);
        case ArchiveFormat::Zip:
            return std::make_shared<ZipExtract>(std::move(pLog), bComputeHash);
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
            shared_ptr<ResourceStream> pResStream = make_shared<ResourceStream>(_L_);

            if (!pResStream)
            {
                return E_OUTOFMEMORY;
            }

            wstring strFileName(pwzZipFilePath);
            wstring ResName, MotherShip, NameInArchive, Format;

            if (FAILED(
                    hr = EmbeddedResource::SplitResourceReference(
                        _L_, strFileName, MotherShip, ResName, NameInArchive, Format)))
                return hr;

            HMODULE hMod = NULL;
            HRSRC hRes = NULL;
            std::wstring strBinaryPath;

            if (FAILED(
                    hr = EmbeddedResource::LocateResource(
                        _L_, MotherShip, ResName, EmbeddedResource::BINARY(), hMod, hRes, strBinaryPath)))
                return hr;

            if (FAILED(hr = pResStream->OpenForReadOnly(hMod, hRes)))
                return hr;

            stream = pResStream;
        }
        else
        {
            shared_ptr<FileStream> pFileStream = make_shared<FileStream>(_L_);

            if (!pFileStream)
                return E_OUTOFMEMORY;
            ;

            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = FALSE;
            sa.lpSecurityDescriptor = NULL;

            SecurityDescriptor sd(_L_);
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
        [pwzExtractRootDir, szSDDL, this](Archive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
        HRESULT hr = E_FAIL;

        HANDLE hFile = INVALID_HANDLE_VALUE;
        if (FAILED(
                hr = UtilGetUniquePath(
                    _L_,
                    pwzExtractRootDir,
                    item.NameInArchive.c_str(),
                    item.Path,
                    hFile,
                    FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                    szSDDL)))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to get unique path for file %s in %s\r\n",
                item.NameInArchive.c_str(),
                pwzExtractRootDir);
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

        WCHAR szDirectory[MAX_PATH];

        if (FAILED(hr = GetDirectoryForFile(item.Path.c_str(), szDirectory, MAX_PATH)))
        {
            log::Error(_L_, hr, L"Failed to get directory for file %s\r\n", item.Path.c_str());
            return nullptr;
        }
        fs::path absolute(szDirectory);

        try
        {
            fs::create_directories(absolute);
        }
        catch (const fs::filesystem_error& error)
        {
            const HRESULT hr = HRESULT_FROM_WIN32(error.code().value());
            log::Error(
                _L_,
                hr,
                L"Failed to create directory %s while processing file %s\r\n",
                absolute.c_str(),
                item.Path.c_str());
            return nullptr;
        }

        auto filestream = make_shared<FileStream>(_L_);
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
            auto newfs = std::make_shared<FileStream>(_L_);

            if (FAILED(hr = newfs->Duplicate(*fs)))
                return hr;

            stream = newfs;

            return S_OK;
        }

        std::shared_ptr<MemoryStream> ms = std::dynamic_pointer_cast<MemoryStream>(pArchiveStream);

        if (ms)
        {
            auto newms = std::make_shared<MemoryStream>(_L_);

            if (FAILED(hr = newms->Duplicate(*ms)))
                return hr;

            stream = newms;

            return S_OK;
        }

        return S_OK;
    };

    auto MakeWriteStream =
        [pwzExtractRootDir, szSDDL, this](Archive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
        HRESULT hr = E_FAIL;

        HANDLE hFile = INVALID_HANDLE_VALUE;

        if (FAILED(
                hr = UtilGetUniquePath(
                    _L_,
                    pwzExtractRootDir,
                    item.NameInArchive.c_str(),
                    item.Path,
                    hFile,
                    FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                    szSDDL)))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to get unique path for file %s in %s\r\n",
                item.NameInArchive.c_str(),
                pwzExtractRootDir);
            return nullptr;
        }

        WCHAR szDirectory[MAX_PATH];

        if (FAILED(hr = GetDirectoryForFile(item.Path.c_str(), szDirectory, MAX_PATH)))
        {
            log::Error(_L_, hr, L"Failed to get directory for file %s\r\n", item.Path.c_str());
            return nullptr;
        }
        fs::path absolute(szDirectory);

        try
        {
            fs::create_directories(absolute);
        }
        catch (const fs::filesystem_error& error)
        {
            auto code = error.code();
            return nullptr;
        }

        auto filestream = make_shared<FileStream>(_L_);
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
        [pwzExtractRootDir, szSDDL, this](Archive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
        HRESULT hr = E_FAIL;

        HANDLE hFile = INVALID_HANDLE_VALUE;

        if (FAILED(
                hr = UtilGetUniquePath(
                    _L_,
                    pwzExtractRootDir,
                    item.NameInArchive.c_str(),
                    item.Path,
                    hFile,
                    FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                    szSDDL)))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to get unique path for file %s in %s\r\n",
                item.NameInArchive.c_str(),
                pwzExtractRootDir);
            return nullptr;
        }

        WCHAR szDirectory[MAX_PATH];

        if (FAILED(hr = GetDirectoryForFile(item.Path.c_str(), szDirectory, MAX_PATH)))
        {
            log::Error(_L_, hr, L"Failed to get directory for file %s\r\n", item.Path.c_str());
            return nullptr;
        }
        fs::path absolute(szDirectory);

        try
        {
            fs::create_directories(absolute);
        }
        catch (const fs::filesystem_error& exception)
        {
            auto code = exception.code();
            return nullptr;
        }
        auto filestream = make_shared<FileStream>(_L_);
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
