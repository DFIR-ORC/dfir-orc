//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ArchiveMessage.h"

using namespace std;

using namespace Orc;

struct ArchiveMessage_make_shared_enabler : public Orc::ArchiveMessage {
	inline ArchiveMessage_make_shared_enabler(Orc::ArchiveMessage::Request request) : Orc::ArchiveMessage(request) {};
};


ArchiveMessage::Message
ArchiveMessage::MakeOpenRequest(const std::wstring& strArchiveName, const std::wstring& strCompressionLevel)
{
    auto retval = make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::OpenArchive);
    retval->m_Name = strArchiveName;
    retval->m_CompressionLevel = strCompressionLevel;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeOpenRequest(
    const std::wstring& strArchiveName,
    const ArchiveFormat aFormat,
    const std::shared_ptr<ByteStream>& aStream,
    const std::wstring& strCompressionLevel)
{
    auto retval = make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::OpenArchive);
    retval->m_Name = strArchiveName;
    retval->m_Format = aFormat;
    retval->m_Stream = aStream;
    retval->m_CompressionLevel = strCompressionLevel;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeOpenRequest(const OutputSpec& anOutput)
{
    if (anOutput.Type != OutputSpec::Kind::Archive)
        return nullptr;

    auto retval = make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::OpenArchive);
    retval->m_Name = anOutput.Path;
    retval->m_Format = anOutput.ArchiveFormat;
    retval->m_CompressionLevel = anOutput.Compression;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeAddFileRequest(
    const std::wstring& szNameInArchive,
    const std::wstring& szFileName,
    bool bHashData,
    DWORD dwXORPattern,
    bool bDeleteWhenDone)
{
    auto retval = make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::AddFile);
    retval->m_Status = ArchiveMessage::Ready;
    retval->m_Keyword = szNameInArchive;
    retval->m_Name = szFileName;
    retval->m_bHashData = bHashData;
    retval->m_dwXORPattern = dwXORPattern;
    retval->m_bDeleteWhenDone = bDeleteWhenDone;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeAddDirectoryRequest(
    const std::wstring& szCabbedName,
    const std::wstring& szDirName,
    const std::wstring& szPattern,
    bool bHashData,
    DWORD dwXORPattern,
    bool bDeleteOnCompletion)
{
    auto retval = make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::AddDirectory);
    retval->m_Status = ArchiveMessage::Ready;
    retval->m_Keyword = szCabbedName;
    retval->m_Name = szDirName;
    retval->m_Pattern = szPattern;
    retval->m_bHashData = bHashData;
    retval->m_dwXORPattern = dwXORPattern;
    retval->m_bDeleteWhenDone = bDeleteOnCompletion;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeAddStreamRequest(
    const std::wstring& szCabbedName,
    const std::shared_ptr<ByteStream>& aStream,
    bool bHashData,
    DWORD dwXORPattern)
{
    auto retval = make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::AddStream);
    retval->m_Status = ArchiveMessage::Wait;
    retval->m_Keyword = szCabbedName;
    retval->m_Name = szCabbedName;
    retval->m_bHashData = bHashData;
    retval->m_dwXORPattern = dwXORPattern;
    retval->m_Stream = aStream;
    return retval;
}

ArchiveMessage::Message ArchiveMessage::MakeFlushQueueRequest()
{
    return make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::FlushQueue);
}

ArchiveMessage::Message ArchiveMessage::MakeCompleteRequest()
{
    return make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::Complete);
}

ArchiveMessage::Message ArchiveMessage::MakeCancellationRequest()
{
    return make_shared<ArchiveMessage_make_shared_enabler>(ArchiveMessage::Cancel);
}

ArchiveMessage::~ArchiveMessage(void) {}
