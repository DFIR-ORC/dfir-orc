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
    Stream(logger pLog);

    HRESULT Open(std::shared_ptr<ByteStream> stream);

    virtual arrow::Status GetSize(int64_t* size) override final;

    virtual bool supports_zero_copy() const override final { return false; };

    virtual arrow::Status Read(int64_t nbytes, int64_t* bytes_read, void* out) override final;
    virtual arrow::Status Read(int64_t nbytes, std::shared_ptr<arrow::Buffer>* out) override final;

    virtual arrow::Status ReadAt(int64_t position, int64_t nbytes, int64_t* bytes_read, void* out) override final;
    virtual arrow::Status ReadAt(int64_t position, int64_t nbytes, std::shared_ptr<arrow::Buffer>* out) override final;

    virtual arrow::Status Seek(int64_t position) override final;
    virtual arrow::Status Tell(int64_t* position) const override final;

    virtual arrow::Status Write(const void* data, int64_t nbytes) override final;
    virtual arrow::Status WriteAt(int64_t position, const void* data, int64_t nbytes) override final;

    virtual arrow::Status Flush() override final;

    virtual arrow::Status Close() override final;

    virtual bool closed() const override final;

    virtual ~Stream() override;

private:
    std::shared_ptr<ByteStream> m_Stream;

    logger _L_;
};

}  // namespace Orc::TableOutput::Parquet