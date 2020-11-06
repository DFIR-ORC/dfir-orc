//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ByteStream.h"
#include "ParquetDefinitions.h"

namespace Orc::TableOutput::Parquet {

class Stream : public arrow::io::ReadWriteFileInterface
{
public:
    Stream();

    HRESULT Open(std::shared_ptr<ByteStream> stream);

    arrow::Result<int64_t> GetSize() override final;

    bool supports_zero_copy() const override final { return false; };

    arrow::Result<int64_t> Read(int64_t nbytes, void* out) override final;
    arrow::Result<std::shared_ptr<arrow::Buffer>> Read(int64_t nbytes) override final;

    arrow::Result<int64_t> ReadAt(int64_t position, int64_t nbytes, void* out) override final;
    arrow::Result<std::shared_ptr<arrow::Buffer>> ReadAt(int64_t position, int64_t nbytes) override final;

    arrow::Status Seek(int64_t position) override final;
    arrow::Result<int64_t> Tell() const override final;

    arrow::Status Write(const void* data, int64_t nbytes) override final;
    arrow::Status WriteAt(int64_t position, const void* data, int64_t nbytes) override final;

    arrow::Status Flush() override final;

    arrow::Status Close() override final;

    bool closed() const override final;

    ~Stream() override;

private:
    std::shared_ptr<ByteStream> m_Stream;
};

}  // namespace Orc::TableOutput::Parquet
