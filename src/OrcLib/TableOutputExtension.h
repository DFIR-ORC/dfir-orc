//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ExtensionLibrary.h"

#include "TableOutputWriter.h"

#pragma managed(push, off)

namespace Orc {

class TableOutputExtension : public ExtensionLibrary
{

    friend class ExtensionLibrary;

public:
    TableOutputExtension(std::wstring strFormat, std::wstring strX86Lib, std::wstring strX64Lib)
        : ExtensionLibrary(std::move(strFormat), std::move(strX86Lib), std::move(strX64Lib)) {};

    bool IsStreamTableOutput() { return m_StreamTableFactory != nullptr; }
    bool IsConnectionTableOutput() { return m_ConnectTableFactory != nullptr; }

    template <typename... Args>
    auto ConnectionFactory(Args&&... args)
    {
        return Call(m_ConnectionFactory, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto ConnectTableFactory(Args&&... args)
    {
        return Call(m_ConnectTableFactory, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto StreamTableFactory(Args&&... args)
    {
        return Call(m_StreamTableFactory, std::forward<Args>(args)...);
    }

    STDMETHOD(Initialize)();

    virtual ~TableOutputExtension();

private:
    std::shared_ptr<TableOutput::IConnection>(WINAPI* m_ConnectionFactory)(
        std::unique_ptr<TableOutput::Options>&& options);
    std::shared_ptr<TableOutput::IConnectWriter>(WINAPI* m_ConnectTableFactory)(
        std::unique_ptr<TableOutput::Options>&& options);
    std::shared_ptr<TableOutput::IStreamWriter>(WINAPI* m_StreamTableFactory)(
        std::unique_ptr<TableOutput::Options>&& options);
};
}  // namespace Orc

#pragma managed(pop)
