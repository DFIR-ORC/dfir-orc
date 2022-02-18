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

using namespace Orc::Command::Wolf;
using namespace Orc;

namespace {

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const std::optional<IO_COUNTERS>& counters)
{
    if (!counters)
    {
        return;
    }

    const auto kRoot = L"io_counters";
    writer->BeginElement(kRoot);
    Guard::Scope onExit([&]() { writer->EndElement(kRoot); });

    writer->WriteNamed(L"read_operation", counters->ReadOperationCount);
    writer->WriteNamed(L"read_transfer", counters->ReadTransferCount);
    writer->WriteNamed(L"write_operation", counters->WriteOperationCount);
    writer->WriteNamed(L"write_transfer", counters->WriteTransferCount);
    writer->WriteNamed(L"other_operation", counters->OtherOperationCount);
    writer->WriteNamed(L"other_transfer", counters->OtherTransferCount);
}

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const std::optional<Outcome::JobStatistics>& statistics)
{
    if (!statistics)
    {
        return;
    }

    const auto kRoot = L"statistics";
    writer->BeginElement(kRoot);
    Guard::Scope onExit([&]() { writer->EndElement(kRoot); });

    Write(writer, statistics->GetIOCounters());

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
        writer->WriteNamed(L"size", *size);
    }
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

    const auto& size = archive.GetSize();
    if (size.has_value())
    {
        writer->WriteNamed(L"size", *size);
    }

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

void Write(StructuredOutputWriter::IWriter::Ptr& writer, const Outcome::Command& command)
{
    writer->BeginElement(nullptr);
    Guard::Scope onExit([&]() { writer->EndElement(nullptr); });

    writer->WriteNamed(L"name", command.GetKeyword());
    writer->WriteNamed(L"command_line", command.GetCommandLineValue());

    const auto start = ToStringIso8601(command.GetCreationTime());
    if (start.has_value())
    {
        writer->WriteNamed(L"start", *start);
    }

    const auto end = ToStringIso8601(command.GetExitTime());
    if (end.has_value())
    {
        writer->WriteNamed(L"end", *end);
    }

    writer->WriteNamed(L"exit_code", command.GetExitCode());
    writer->WriteNamed(L"pid", command.GetPid());

    const auto& userTime = command.GetUserTime();
    if (userTime)
    {
        writer->WriteNamed(L"user_time", userTime->count());
    }

    const auto& kernelTime = command.GetUserTime();
    if (kernelTime)
    {
        writer->WriteNamed(L"kernel_time", kernelTime->count());
    }

    ::Write(writer, command.GetIOCounters());
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

    ::Write(writer, commandSet.GetJobStatistics());

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
