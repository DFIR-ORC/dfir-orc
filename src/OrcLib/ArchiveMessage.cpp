//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ArchiveMessage.h"

using namespace Orc;

namespace {

// Enable the use of std::make_shared with ArchiveMessage protected constructor
struct ArchiveMessageT : public Orc::ArchiveMessage
{
    template <typename... Args>
    inline ArchiveMessageT(Args&&... args)
        : ArchiveMessage(std::forward<Args>(args)...)
    {
    }
};

}  // namespace

ArchiveMessage::Message
ArchiveMessage::MakeOpenRequest(const std::wstring& archiveFileName, const std::wstring& compressionLevel)
{
    auto retval = std::make_shared<::ArchiveMessageT>(ArchiveMessage::OpenArchive);
    retval->m_archiveFileName = archiveFileName;
    retval->m_compressionLevel = compressionLevel;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeOpenRequest(
    const std::wstring& archiveName,
    const ArchiveFormat format,
    const std::shared_ptr<ByteStream>& stream,
    const std::wstring& compressionLevel,
    const std::wstring& password)
{
    auto retval = std::make_shared<::ArchiveMessageT>(ArchiveMessage::OpenArchive);
    retval->m_archiveFileName = archiveName;
    retval->m_format = format;
    retval->m_stream = stream;
    retval->m_compressionLevel = compressionLevel;
    retval->m_password = password;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeOpenRequest(const OutputSpec& output)
{
    if (output.Type != OutputSpec::Kind::Archive)
    {
        return nullptr;
    }

    auto retval = std::make_shared<::ArchiveMessageT>(ArchiveMessage::OpenArchive);
    retval->m_archiveFileName = output.Path;
    retval->m_format = output.ArchiveFormat;
    retval->m_compressionLevel = output.Compression;
    retval->m_password = output.Password;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeAddFileRequest(
    const std::wstring& nameInArchive,
    const std::wstring& fileName,
    bool bHashData,
    bool bDeleteWhenDone)
{
    auto retval = std::make_shared<::ArchiveMessageT>(ArchiveMessage::AddFile);
    retval->m_status = ArchiveMessage::Ready;
    retval->m_nameInArchive = nameInArchive;
    retval->m_sourcePath = fileName;
    retval->m_bHashData = bHashData;
    retval->m_bDeleteWhenDone = bDeleteWhenDone;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeAddDirectoryRequest(
    const std::wstring& nameInArchive,
    const std::wstring& dirName,
    const std::wstring& pattern,
    bool bHashData,
    bool bDeleteOnCompletion)
{
    auto retval = std::make_shared<::ArchiveMessageT>(ArchiveMessage::AddDirectory);
    retval->m_status = ArchiveMessage::Ready;
    retval->m_nameInArchive = nameInArchive;
    retval->m_sourcePath = dirName;
    retval->m_pattern = pattern;
    retval->m_bHashData = bHashData;
    retval->m_bDeleteWhenDone = bDeleteOnCompletion;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeAddStreamRequest(
    const std::wstring& nameInArchive,
    const std::shared_ptr<ByteStream>& aStream,
    bool bHashData)
{
    auto retval = std::make_shared<::ArchiveMessageT>(ArchiveMessage::AddStream);
    retval->m_status = ArchiveMessage::Wait;
    retval->m_sourcePath = nameInArchive;
    retval->m_nameInArchive = nameInArchive;
    retval->m_bHashData = bHashData;
    retval->m_stream = aStream;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeFlushQueueRequest()
{
    return std::make_shared<::ArchiveMessageT>(ArchiveMessage::FlushQueue);
}

ArchiveMessage::Message ArchiveMessage::MakeCompleteRequest()
{
    return std::make_shared<::ArchiveMessageT>(ArchiveMessage::Complete);
}

ArchiveMessage::Message ArchiveMessage::MakeCancellationRequest()
{
    return std::make_shared<::ArchiveMessageT>(ArchiveMessage::Cancel);
}

ArchiveMessage::~ArchiveMessage() {}
