//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "TaskUtils.h"

#include <memory>

#include <agents.h>

#include "StreamMessage.h"
#include "StreamAgent.h"

class VolumeReader;
class MFTRecord;
class DataAttribute;

class NTFSAgent : public StreamAgent
{
private:
    std::shared_ptr<VolumeReader> m_pVolReader;
    MFTRecord* m_pRecord;
    std::shared_ptr<DataAttribute> m_pDataAttr;

    ULONGLONG m_DataSize;
    ULONGLONG m_CurrentPosition;

protected:
    HRESULT Read(ULONGLONG cbBytesToRead, CBinaryBuffer& buffer);
    HRESULT Write(ULONGLONG cbBytesToWrite, CBinaryBuffer& buffer, ULONGLONG* pBytesWritten);
    HRESULT SetFilePointer(__in LONGLONG lDistanceToMove, __in DWORD dwMoveMethod, __out_opt PLONGLONG pqwCurrPointer);
    HRESULT Close();

public:
    explicit NTFSAgent(LogFileWriter* pW, StreamMessage::ISource& source, StreamMessage::ITarget& target)
        : StreamAgent(pW, source, target)
    {
        m_pVolReader = NULL;
        m_pRecord = NULL;
        m_pDataAttr = NULL;
        m_CurrentPosition = 0;
        m_DataSize = 0;
    }

    __data_entrypoint(File) HRESULT OpenStream(
        __in_opt const std::shared_ptr<VolumeReader>& pVolReader,
        __in_opt MFTRecord* pRecord,
        __in_opt const std::shared_ptr<DataAttribute>& pDataAttr);

    HRESULT IsOpen() { return (m_pDataAttr != NULL) ? S_OK : S_FALSE; };

    ULONGLONG GetSize();

    ~NTFSAgent(void);
};
