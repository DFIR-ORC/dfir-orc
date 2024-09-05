//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ByteStream.h"

#pragma warning(disable : 4521)
#include "orc/OrcFile.hh"
#pragma warning(default : 4521)

namespace Orc::TableOutput::ApacheOrc {

class Stream : public orc::OutputStream
{
public:
    Stream() {}

    HRESULT Open(std::shared_ptr<ByteStream> stream)
    {
        if (!stream)
            return E_POINTER;

        if (stream->IsOpen() != S_OK)
            return E_INVALIDARG;

        std::swap(m_Stream, stream);
        return S_OK;
    }

    virtual uint64_t getLength() const override final;

    virtual uint64_t getNaturalWriteSize() const override final;

    virtual void write(const void* buf, size_t length) override final;

    virtual const std::string& getName() const override final { return m_Name; }

    virtual void close() override final;

    ~Stream() {}

private:
    std::shared_ptr<ByteStream> m_Stream;
    std::string m_Name;
};

}  // namespace Orc::TableOutput::ApacheOrc
