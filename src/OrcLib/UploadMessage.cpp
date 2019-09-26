//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "UploadMessage.h"

using namespace std;

using namespace Orc;

struct UploadMessage_make_shared_enabler : public Orc::UploadMessage {
	inline UploadMessage_make_shared_enabler(Orc::UploadMessage::Request request) : UploadMessage(request) {}
};

UploadMessage::Message UploadMessage::MakeUploadFileRequest(
    const std::wstring& strRemoteName,
    const std::wstring& strFileName,
    bool bDeleteWhenDone)
{
    auto retval = make_shared<UploadMessage_make_shared_enabler>(UploadMessage::UploadFile);
    retval->m_Status = UploadMessage::Ready;
    retval->m_Remote = strRemoteName;
    retval->m_Local = strFileName;
    retval->m_bDeleteWhenDone = bDeleteWhenDone;
    return retval;
}

UploadMessage::Message UploadMessage::MakeUploadDirectoryRequest(
    const std::wstring& strRemoteName,
    const std::wstring& strDirName,
    const std::wstring& strPattern,
    bool bDeleteOnCompletion)
{
    auto retval = make_shared<UploadMessage_make_shared_enabler>(UploadMessage::UploadDirectory);
    retval->m_Status = UploadMessage::Ready;
    retval->m_Remote = strRemoteName;
    retval->m_Local = strDirName;
    retval->m_Pattern = strPattern;
    retval->m_bDeleteWhenDone = bDeleteOnCompletion;
    return retval;
}

UploadMessage::Message
UploadMessage::MakeUploadStreamRequest(const std::wstring& strRemoteName, const std::shared_ptr<ByteStream>& aStream)
{
    auto retval = make_shared<UploadMessage_make_shared_enabler>(UploadMessage::UploadStream);
    retval->m_Status = UploadMessage::Ready;
    retval->m_Local = strRemoteName;
    retval->m_Remote = strRemoteName;
    retval->m_Stream = aStream;
    return retval;
}

UploadMessage::Message UploadMessage::MakeRefreshJobStatusRequest()
{
    return make_shared<UploadMessage_make_shared_enabler>(UploadMessage::RefreshJobStatus);
}

UploadMessage::Message UploadMessage::MakeCompleteRequest()
{
    return make_shared<UploadMessage_make_shared_enabler>(UploadMessage::Complete);
}

UploadMessage::Message UploadMessage::MakeCancellationRequest()
{
    return make_shared<UploadMessage_make_shared_enabler>(UploadMessage::Cancel);
}

UploadMessage::~UploadMessage(void) {}
