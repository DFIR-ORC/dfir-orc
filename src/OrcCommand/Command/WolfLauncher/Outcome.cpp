//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "Command/WolfLauncher/Outcome.h"
#include "Utils/Time.h"
#include "Utils/Guard.h"
#include "Utils/TypeTraits.h"
#include "Text/Fmt/GUID.h"

using namespace Orc::Command::Wolf;
using namespace Orc;

namespace {

std::string ToString(Outcome::Command::Output::Type type)
{
    using Type = Outcome::Command::Output::Type;

    switch (type)
    {
        case Type::StdOut:
            return "stdout";
        case Type::StdErr:
            return "stderr";
        case Type::StdOutErr:
            return "stdout_stderr";
        case Type::File:
            return "file";
        case Type::Directory:
            return "directory";
        case Type::Undefined:
            return "undefined";
        default:
            return "unknown";
    }
}

void Write(
    StructuredOutputWriter::IWriter::Ptr& writer,
    std::wstring_view key,
    const std::vector<Outcome::Command::Output>& output)
{
    writer->BeginCollection(key.data());
    Guard::Scope onOutputExit([&]() { writer->EndCollection(key.data()); });

    for (const auto& item : output)
    {
        writer->BeginElement(nullptr);
        Guard::Scope onExit([&]() { writer->EndElement(nullptr); });

        writer->WriteNamed(L"name", item.GetName());
        writer->WriteNamed(L"type", ToString(item.GetType()));

        const auto& size = item.GetSize();
        if (size.has_value())
        {
            writer->WriteNamed(L"size", static_cast<uint64_t>(*size));
        }
    }
};

void Write(
    StructuredOutputWriter::IWriter::Ptr& writer,
    std::wstring_view key,
    const std::optional<std::chrono::system_clock::time_point>& tp)
{
    if (!tp.has_value())
    {
        return;
    }

    auto rv = ToStringIso8601(*tp);
    if (rv.has_error())
    {
        return;
    }

    writer->WriteNamed(key.data(), rv.value());
}

void Write(
    StructuredOutputWriter::IWriter::Ptr& writer,
    std::wstring_view key,
    const std::optional<IO_COUNTERS>& counters)
{
    if (!counters)
    {
        return;
    }

    writer->BeginElement(key.data());
    Guard::Scope onExit([&]() { writer->EndElement(key.data()); });

    writer->WriteNamed(L"read_operation", counters->ReadOperationCount);
    writer->WriteNamed(L"read_transfer", counters->ReadTransferCount);
    writer->WriteNamed(L"write_operation", counters->WriteOperationCount);
    writer->WriteNamed(L"write_transfer", counters->WriteTransferCount);
    writer->WriteNamed(L"other_operation", counters->OtherOperationCount);
    writer->WriteNamed(L"other_transfer", counters->OtherTransferCount);
}

void Write(
    StructuredOutputWriter::IWriter::Ptr& writer,
    std::wstring_view key,
    const std::optional<Outcome::JobStatistics>& statistics)
{
    if (!statistics)
    {
        return;
    }

    writer->BeginElement(key.data());
    Guard::Scope onExit([&]() { writer->EndElement(key.data()); });

    Write(writer, L"io_counters", statistics->GetIOCounters());

    writer->WriteNamed(L"process", statistics->GetProcessCount());
    writer->WriteNamed(L"process_memory_peak", statistics->GetPeakProcessMemory());
    writer->WriteNamed(L"job_memory_peak", statistics->GetPeakJobMemory());
    writer->WriteNamed(L"active_proces", statistics->GetActiveProcessCount());
    writer->WriteNamed(L"terminated_process", statistics->GetTerminatedProcessCount());
    writer->WriteNamed(L"page_fault", statistics->GetPageFaultCount());
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const Outcome::Archive::Item& item)
{
    writer->WriteNamed(L"name", item.GetName());

    const auto& size = item.GetSize();
    if (size.has_value())
    {
        writer->WriteNamed(L"size", size.value().value);
    }
}

std::string ToString(Outcome::Archive::InputType inputType)
{
    switch (inputType)
    {
        case Outcome::Archive::InputType::kUndefined:
            return "undefined";
        case Outcome::Archive::InputType::kOffline:
            return "offline";
        case Outcome::Archive::InputType::kRunningSystem:
            return "running_system";
    }

    return "<unknown>";
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const Outcome::Archive& archive)
{
    if (archive.GetName().empty())
    {
        return;
    }

    const auto kRoot = L"archive";
    writer->BeginElement(kRoot);
    Guard::Scope onExit([&]() { writer->EndElement(kRoot); });

    writer->WriteNamed(L"name", archive.GetName());

    const auto& sha1 = archive.GetSha1();
    if (sha1.has_value())
    {
        writer->WriteNamed(L"sha1", *sha1);
    }

    const auto& size = archive.GetSize();
    if (size.has_value())
    {
        writer->WriteNamed(L"size", static_cast<uint64_t>(size.value()));
    }

    writer->WriteNamed(L"input_type", ToString(archive.InputTypeValue()));

    {
        writer->BeginCollection(L"files");
        Guard::Scope onExit([&]() { writer->EndCollection(L"files"); });

        for (const auto& item : archive.GetItems())
        {
            writer->BeginElement(nullptr);
            Guard::Scope onExit([&]() { writer->EndElement(nullptr); });

            Write(writer, item);
        }
    }
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, std::wstring_view key, const Outcome::Command::Origin& origin)
{
    writer->BeginElement(key.data());
    Guard::Scope onExit([&]() { writer->EndElement(key.data()); });

    if (origin.GetFriendlyName())
    {
        writer->WriteNamed(L"friendly_name", *origin.GetFriendlyName());
    }

    if (origin.GetResourceName())
    {
        writer->WriteNamed(L"type", L"resource");

        const auto kNodeLocation = L"location";
        writer->BeginElement(kNodeLocation);
        Guard::Scope onLocationExit([&]() { writer->EndElement(kNodeLocation); });

        writer->WriteNamed(L"name", *origin.GetResourceName());
    }
    else
    {
        writer->WriteNamed(L"type", L"file");
    }
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, std::wstring_view key, std::optional<uint32_t> value)
{
    if (!value)
    {
        return;
    }

    writer->WriteNamed(key.data(), *value);
}

void Write(
    StructuredOutputWriter::IWriter::Ptr& writer,
    std::wstring_view key,
    const std::optional<std::wstring>& value)
{
    if (!value)
    {
        return;
    }

    writer->WriteNamed(key.data(), *value);
}

void Write(
    StructuredOutputWriter::IWriter::Ptr& writer,
    std::wstring_view key,
    const std::optional<std::chrono::seconds>& value)
{
    if (!value)
    {
        return;
    }

    writer->WriteNamed(key.data(), value->count());
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const Outcome::Command& command)
{
    writer->BeginElement(nullptr);
    Guard::Scope onExit([&]() { writer->EndElement(nullptr); });

    writer->WriteNamed(L"name", command.GetKeyword());
    ::Write(writer, L"command_line", command.GetCommandLineValue());

    if (command.IsSelfOrcExecutable() && command.GetOrcTool())
    {
        writer->WriteNamed(L"tool", *command.GetOrcTool());
    }

    ::Write(writer, L"start", command.GetCreationTime());
    ::Write(writer, L"end", command.GetExitTime());
    ::Write(writer, L"exit_code", command.GetExitCode());
    ::Write(writer, L"pid", command.GetPid());
    ::Write(writer, L"sha1", command.GetSha1());
    ::Write(writer, L"origin", command.GetOrigin());
    ::Write(writer, L"user_time", command.GetUserTime());
    ::Write(writer, L"kernel_time", command.GetKernelTime());
    ::Write(writer, L"io_counters", command.GetIOCounters());
    ::Write(writer, L"output", command.GetOutput());
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const Outcome::CommandSet& commandSet)
{
    writer->BeginElement(nullptr);
    Guard::Scope onExit([&]() { writer->EndElement(nullptr); });

    writer->WriteNamed(L"name", commandSet.GetKeyword());

    const auto start = ToStringIso8601(commandSet.GetStart());
    if (start.has_value())
    {
        writer->WriteNamed(L"start", *start);
    }

    const auto end = ToStringIso8601(commandSet.GetEnd());
    if (end.has_value())
    {
        writer->WriteNamed(L"end", *end);
    }

    ::Write(writer, L"statistics", commandSet.GetJobStatistics());

    ::Write(writer, commandSet.GetArchive());

    {
        writer->BeginCollection(L"commands");
        Guard::Scope onExit([&]() { writer->EndCollection(L"commands"); });

        for (const auto& commandRef : commandSet.GetCommands())
        {
            ::Write(writer, commandRef.get());
        }
    }
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const Outcome::Mothership& mothership)
{
    const auto kRoot = L"mothership";
    writer->BeginElement(kRoot);
    Guard::Scope onExit([&]() { writer->EndElement(kRoot); });

    writer->WriteNamed(L"sha1", mothership.GetSha1());
    writer->WriteNamed(L"command_line", mothership.GetCommandLineValue());
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const Outcome::WolfLauncher& wolfLauncher)
{
    const auto kNodeWolfLauncher = L"wolf_launcher";
    writer->BeginElement(kNodeWolfLauncher);
    Guard::Scope onExit([&]() { writer->EndElement(kNodeWolfLauncher); });

    writer->WriteNamed(L"sha1", wolfLauncher.GetSha1());
    writer->WriteNamed(L"version", wolfLauncher.GetVersion());
    writer->WriteNamed(L"command_line", wolfLauncher.GetCommandLineValue());
}

}  // namespace

namespace Orc::Command::Wolf::Outcome {

Orc::Result<void> Write(const Outcome& outcome, StructuredOutputWriter::IWriter::Ptr& writer)
{
    try
    {
        writer->WriteNamed(L"version", L"1.0");

        const auto kNodeDfirOrc = L"dfir-orc";
        writer->BeginElement(kNodeDfirOrc);
        Guard::Scope onExit([&]() { writer->EndElement(kNodeDfirOrc); });
        {
            const auto kNodeOutcome = L"outcome";
            writer->BeginElement(kNodeOutcome);
            Guard::Scope onExit([&]() { writer->EndElement(kNodeOutcome); });

            std::wstring id;
            Orc::ToString(outcome.GetId(), std::back_inserter(id));
            writer->WriteNamed(L"id", id);

            auto timestamp = outcome.GetTimestampKey();
            writer->WriteNamed(L"timestamp", timestamp);

            auto startingTime = ToStringIso8601(outcome.GetStartingTime());
            if (startingTime.has_value())
            {
                writer->WriteNamed(L"start", *startingTime);
            }

            auto endingTime = ToStringIso8601(outcome.GetEndingTime());
            if (endingTime.has_value())
            {
                writer->WriteNamed(L"end", *endingTime);
            }

            writer->WriteNamed(L"computer_name", outcome.GetComputerNameValue());

            ::Write(writer, outcome.GetMothership());

            ::Write(writer, outcome.GetWolfLauncher());

            {
                const auto kNodeConsole = L"console";
                writer->BeginElement(kNodeConsole);
                Guard::Scope onLogExit([&]() { writer->EndElement(kNodeConsole); });

                {
                    const auto kNodeConsoleFile = L"file";
                    writer->BeginElement(kNodeConsoleFile);
                    Guard::Scope onConsoleFileExit([&]() { writer->EndElement(kNodeConsoleFile); });

                    writer->WriteNamed(L"name", outcome.ConsoleFileName());
                }
            }

            {
                const auto kNodeLog = L"log";
                writer->BeginElement(kNodeLog);
                Guard::Scope onLogExit([&]() { writer->EndElement(kNodeLog); });

                {
                    const auto kNodeLogFile = L"file";
                    writer->BeginElement(kNodeLogFile);
                    Guard::Scope onLogFileExit([&]() { writer->EndElement(kNodeLogFile); });

                    writer->WriteNamed(L"name", outcome.LogFileName());
                }
            }

            {
                const auto kNodeOutline = L"outline";
                writer->BeginElement(kNodeOutline);
                Guard::Scope onLogExit([&]() { writer->EndElement(kNodeOutline); });

                {
                    const auto kNodeOulineFile = L"file";
                    writer->BeginElement(kNodeOulineFile);
                    Guard::Scope onLogFileExit([&]() { writer->EndElement(kNodeOulineFile); });

                    writer->WriteNamed(L"name", outcome.OutlineFileName());
                }
            }

            {
                const auto kNodeRecipients = L"recipients";
                writer->BeginCollection(kNodeRecipients);

                Guard::Scope onRecipientsExit([&]() { writer->EndCollection(kNodeRecipients); });

                for (const auto& recipient : outcome.Recipients())
                {
                    writer->BeginElement(nullptr);
                    Guard::Scope onRecipientItem([&]() { writer->EndElement(nullptr); });

                    writer->WriteNamed(L"name", recipient.Name());
                    writer->WriteNamed(L"public_key", recipient.PublicKey());
                }
            }

            {
                const auto kNodeCommandSet = L"command_set";
                writer->BeginCollection(kNodeCommandSet);
                Guard::Scope onExit([&]() { writer->EndCollection(kNodeCommandSet); });

                for (const auto& commandSetRef : outcome.GetCommandSets())
                {
                    ::Write(writer, commandSetRef.get());
                }
            }
        }
    }
    catch (const std::system_error& e)
    {
        return e.code();
    }
    catch (const std::exception& e)
    {
        std::cerr << "std::exception during outcome creation" << std::endl;
        std::cerr << "Caught: " << e.what() << std::endl;
        std::cerr << "Type: " << typeid(e).name() << std::endl;
        return std::errc::state_not_recoverable;
    }
    catch (...)
    {
        std::cerr << "Exception during outcome creation" << std::endl;
        return std::errc::state_not_recoverable;
    }

    return Success<void>();
}

}  // namespace Orc::Command::Wolf::Outcome
