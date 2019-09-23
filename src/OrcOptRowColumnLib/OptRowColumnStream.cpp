//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OptRowColumnStream.h"

uint64_t Orc::TableOutput::OptRowColumn::Stream::getLength() const
{
    if (m_Stream)
        return m_Stream->GetSize();

    return 0LLU;
}

uint64_t Orc::TableOutput::OptRowColumn::Stream::getNaturalWriteSize() const
{
    return DEFAULT_READ_SIZE;
}

void Orc::TableOutput::OptRowColumn::Stream::write(const void* buf, size_t length)
{
    if (m_Stream)
    {
        ULONGLONG ullBytesWritten = 0LLU;

        if (auto hr = m_Stream->Write((PVOID)buf, length, &ullBytesWritten); FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to write into orc file");
        }
    }
}

void Orc::TableOutput::OptRowColumn::Stream::close()
{
    if (m_Stream)
        m_Stream->Close();
}