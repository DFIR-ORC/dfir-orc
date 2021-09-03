//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ParquetStream.h"

#include "ParquetDefinitions.h"

using namespace arrow;
using namespace std::string_literals;

using namespace Orc;
using namespace Orc::TableOutput::Parquet;

// arrow::util::string_view arrow::io::InputStream::Peek(int64_t) const {
//	return std::string_view();
//}

Orc::TableOutput::Parquet::Stream::Stream() {}

HRESULT Orc::TableOutput::Parquet::Stream::Open(std::shared_ptr<ByteStream> stream)
{
    if (!stream)
        return E_POINTER;

    if (stream->IsOpen() != S_OK)
        return E_INVALIDARG;

    std::swap(m_Stream, stream);
    return S_OK;
}

arrow::Result<int64_t> Orc::TableOutput::Parquet::Stream::GetSize()
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    return m_Stream->GetSize();
}

arrow::Result<int64_t> Orc::TableOutput::Parquet::Stream::Read(int64_t nbytes, void* out)
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    ULONGLONG bytesRead = 0LLU;
    auto hr = m_Stream->Read(out, nbytes, &bytesRead);

    if (FAILED(hr))
        return Status::IOError("Failed to read stream"s);

    return bytesRead;
}

arrow::Result<std::shared_ptr<arrow::Buffer>> Orc::TableOutput::Parquet::Stream::Read(int64_t nbytes)
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    auto buffer = arrow::AllocateResizableBuffer(nbytes, arrow::default_memory_pool());
    if (!buffer.ok())
    {
        return buffer.status();
    }

    auto pBuffer = buffer.MoveValueUnsafe();
    auto bytes_read = Read(nbytes, pBuffer->mutable_data());
    if (!bytes_read.ok())
    {
        return bytes_read.status();
    }

    auto status = pBuffer->Resize(bytes_read.ValueUnsafe(), false);
    if (!status.ok())
    {
        return status;
    }

    return pBuffer;
}

arrow::Result<int64_t> Orc::TableOutput::Parquet::Stream::ReadAt(int64_t position, int64_t nbytes, void* out)
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    ULONG64 newPosition = 0LLU;
    auto hr = m_Stream->SetFilePointer(position, FILE_BEGIN, &newPosition);
    if (FAILED(hr) && newPosition != position)
        return Status::IOError("Failed to set position to read"s);

    ULONGLONG bytesRead = 0LLU;
    hr = m_Stream->Read(out, nbytes, &bytesRead);

    if (FAILED(hr))
        return Status::IOError("Failed to read stream"s);

    return bytesRead;
}

arrow::Result<std::shared_ptr<arrow::Buffer>>
Orc::TableOutput::Parquet::Stream::ReadAt(int64_t position, int64_t nbytes)
{
    if (FAILED(m_Stream->SetFilePointer(position, FILE_BEGIN, NULL)))
        return Status::IOError("Failed to set file pointer on arrow stream"s);

    return Read(nbytes);
}

arrow::Status Orc::TableOutput::Parquet::Stream::Seek(int64_t position)
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    ULONG64 newPosition = 0LLU;
    auto hr = m_Stream->SetFilePointer(position, FILE_BEGIN, &newPosition);
    if (FAILED(hr) && newPosition != position)
        return Status::IOError("Failed to set position"s);

    return Status::OK();
}

arrow::Result<int64_t> Orc::TableOutput::Parquet::Stream::Tell() const
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    ULONG64 currentPos = 0LLU;
    auto hr = E_FAIL;
    if (FAILED(hr = m_Stream->SetFilePointer(0LL, FILE_CURRENT, &currentPos)))
    {
        return Status::IOError("Failed to retrieve curent position"s);
    }

    return currentPos;
}

arrow::Status Orc::TableOutput::Parquet::Stream::Write(const void* data, int64_t nbytes)
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    ULONGLONG writtenBytes = 0LLU;
    auto hr = m_Stream->Write((PVOID)data, nbytes, &writtenBytes);
    if (FAILED(hr) || writtenBytes != nbytes)
        return Status::IOError("Failed to write"s);

    return Status::OK();
}

arrow::Status Orc::TableOutput::Parquet::Stream::WriteAt(int64_t position, const void* data, int64_t nbytes)
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    ULONG64 newPosition = 0LLU;
    auto hr = m_Stream->SetFilePointer(position, FILE_BEGIN, &newPosition);
    if (FAILED(hr) && newPosition != position)
        return Status::IOError("Failed to set position"s);

    ULONGLONG writtenBytes = 0LLU;
    hr = m_Stream->Write((PVOID)data, nbytes, &writtenBytes);
    if (FAILED(hr) || writtenBytes != nbytes)
        return Status::IOError("Failed to write"s);

    return Status::OK();
}

arrow::Status Orc::TableOutput::Parquet::Stream::Flush()
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);
    return Status::OK();
}

arrow::Status Orc::TableOutput::Parquet::Stream::Close()
{
    if (!m_Stream)
        return Status::Invalid("Invalid stream"s);

    m_Stream->Close();

    return arrow::Status();
}

bool Orc::TableOutput::Parquet::Stream::closed() const
{
    if (m_Stream)
        return m_Stream->IsOpen() == S_FALSE;
    return false;
}

Orc::TableOutput::Parquet::Stream::~Stream()
{
    if (m_Stream)
        m_Stream->Close();
}
