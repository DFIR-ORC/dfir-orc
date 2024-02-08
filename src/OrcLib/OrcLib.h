//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#pragma managed(push, off)

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>

#include <Windows.h>
#include <winsock.h>

#include <Utils/Result.h>

namespace Orc {

constexpr uint16_t ORC_MAX_PATH = 2048;

// Utilities
class Exception;
enum class Severity : short;

// Utilities/Memory
class CBinaryBuffer;

namespace Detail {
template <typename _T, size_t _DeclElts = 1>
class Buffer;
template <typename _T>
class BufferSpan;
}  // namespace Detail
template <typename _T, size_t _DeclElts = 1>
using Buffer = Detail::Buffer<_T, _DeclElts>;

template <typename _T, size_t _DeclElts = 1>
using ResultBuffer = Result<Detail::Buffer<_T, _DeclElts>>;

template <typename _T>
using Span = Detail::BufferSpan<_T>;

using ByteBuffer = Detail::Buffer<BYTE, 16>;
using ResultByteBuffer = Result<Detail::Buffer<BYTE, 16>>;

class CircularStorage;
class HeapStorage;
class ObjectStorage;

// Utilities/Parameters
class OutputSpec;

// Utilities/Resources
class EmbeddedResource;

// Utilites/Strings
struct CaseInsensitive;
struct CaseInsensitiveUnordered;
struct CaseInsensitiveUnorderedAnsi;

// Utilities/System
class ScopedLock;
class CriticalSection;

class DriverMgmt;
class Driver;
using PDriver = std::shared_ptr<Driver>;

struct SYSTEM_HANDLE_INFORMATON;
struct SystemHandleInformationData;
class JobObject;

class MemoryException;
class SystemException;
class ConsoleException;

class Robustness;

class SecurityDescriptor;
class SystemDetails;

class WMI;

// RunningCode
class WinTrustExtension;
class Authenticode;

struct AutoRunItem;
using AutoRunsEnumItemCallback = void(const AutoRunItem& item, LPVOID pContext);
using AutoRunsVector = std::vector<AutoRunItem>;
class AutoRuns;

class MSIExtension;
class PSAPIExtension;

using PidList = std::vector<DWORD>;
struct ModuleInfo;
using ModuleMap = std::map<std::wstring, ModuleInfo, CaseInsensitive>;
using Modules = std::vector<ModuleInfo>;

class RunningCode;

struct ProcessInfo;
using PProcessInfo = ProcessInfo*;
using ProcessVector = std::vector<ProcessInfo>;
class RunningProcesses;

struct ProcessData;
struct DriverData;
struct UserModuleData;
struct NTrackResults;
using PNTrackResults = NTrackResults*;

class WinTrustExtension;

// Registry
class Hive;
class HiveQuery;
struct ValueTypeDefinition;
class RegFind;
class RegFindConfig;

struct RegistryFile;
struct HBINHeader;
struct BlockHeader;
enum KeyType : WORD;
struct KeyHeader;
struct ValuesArray;
struct DataHeader;
struct ValueHeader;
struct HashRecord;
struct LF_LH_Header;
struct NoHashRecord;
struct LI_RI_Header;
struct SKHeader;
class RegistryKey;
class RegistryValue;
class RegistryHive;

// Objects
struct FILE_DIRECTORY_INFORMATION;
using PFILE_DIRECTORY_INFORMATION = FILE_DIRECTORY_INFORMATION*;

class FileDirectory;
class ObjectDirectory;

// In&Out/Archive/SevenZip
class ArchiveExtractCallback;
class ArchiveOpenCallback;
class ArchiveUpdateCallback;
class ZipCreate;
class ZipExtract;

// In&Out/Archive
class OrcArchive;
class ArchiveAgent;

class ArchiveCreate;
class ArchiveExtract;

class ArchiveMessage;
class ArchiveNotification;

// In&Out/ByteStream/CryptoStream
class CryptoHashStream;
class FuzzyHashStream;
class HashStream;
class PasswordEncryptedStream;

// In&Out/ByteStream/FSStream
class FatStream;
class NTFSStream;
class UncompressNTFSStream;

// In&Out/ByteStream/MessageStream
class MessageStream;
class DecodeMessageStream;
class EncodeMessageStream;

// In&Out/ByteStream/SystemStream
class FileMappingStream;
class FileStream;
class PipeStream;
class ResourceStream;

// In&Out/ByteStream/UtilityStream
class AccumulatingStream;
class ChainingStream;
class DevNullStream;
class JournalingStream;
class MemoryStream;
class MultiMemoryStream;
class StringsStream;
class TeeStream;
class TemporaryStream;

// In&Out/ByteStream/WrapperStream
class DiskChunkStream;
class InByteStreamWrapper;
class ISequentialStreamWrapper;
class IStreamWrapper;
class OutByteStreamWrapper;

// In&Out/ByteStream
class ByteStream;

// In&Out/Concurrent
template <class _Type>
class BoundedBuffer;
class BufferAgent;
class ConcurrentStream;
class HashTransform;
template <class _Type>
class MessageQueue;
template <class _Type>
class PriorityQueue;
class Semaphore;
class StreamAgent;
class StreamMessage;

// In&Out/Download
class BITSDownloadTask;
class DownloadTask;
class FileCopyDownloadTask;

// In&Out/StructuredOutput/XML

namespace StructuredOutput {
struct Options;
class Writer;
class IOutput;

namespace XML {
struct Options;
class Writer;
}  // namespace XML

namespace JSON {
struct Options;
}

}  // namespace StructuredOutput

using StructuredOutputOptions = StructuredOutput::Options;
using IStructuredOutput = StructuredOutput::IOutput;

class XmlLiteExtension;
using XmlOutputOptions = StructuredOutput::XML::Options;

using JSONOutputOptions = StructuredOutput::JSON::Options;

// In&Out/TableOutput/CSV
namespace TableOutput::CSV {

class Cruncher;
class FileReader;
class Column;
class Writer;
class Stream;

}  // namespace TableOutput::CSV

// In&Out/TableOutput/Parquet
class ParquetOutputWriter;

// In&Out/TableOutput/Sql
class CsvToSql;
class SqlOutputWriter;

// In&Out/TableOutput
namespace TableOutput {
class Column;
class Schema;
class IOutput;
class IWriter;
}  // namespace TableOutput
using ITableOutput = TableOutput::IOutput;
using ITableWriter = TableOutput::IWriter;

// In&Out/Import
class ImportAgent;
class ImportBytesSemaphore;
class ImportDefinition;
class TableDescription;
using TableDefinition = std::pair<TableDescription, TableOutput::Schema>;
class ImportItem;
class ImportMessage;
class ImportNotification;
class SqlImportAgent;

template <typename Derived, typename Base>
std::unique_ptr<Derived> dynamic_unique_ptr_cast(std::unique_ptr<Base>&& p)
{
    if (Derived* result = dynamic_cast<Derived*>(p.get()))
    {
        p.release();
        return std::unique_ptr<Derived>(result);
    }
    return std::unique_ptr<Derived>(nullptr);
}

template <typename Derived, typename Base, typename Del>
std::unique_ptr<Derived> static_unique_ptr_cast(std::unique_ptr<Base>&& p)
{
    auto d = static_cast<Derived*>(p.release());
    return std::unique_ptr<Derived>(d);
}

struct scope_enter
{
    scope_enter(std::function<void(void)> f) { f(); }
};

struct scope_exit
{
    scope_exit(std::function<void(void)> f)
        : f_(f)
    {
    }
    ~scope_exit(void) { f_(); }

private:
    std::function<void(void)> f_;
};

struct scope
{
    scope(std::function<void(void)> enter, std::function<void(void)> exit)
        : enter_(enter)
        , exit_(exit)
    {
    }

private:
    scope_enter enter_;
    scope_exit exit_;
};

template <typename _resourceT>
struct resource_scope
{

    using _lambdaT = std::function<void(_resourceT&)>;

    resource_scope(_resourceT& r, _lambdaT enter, _lambdaT exit)
        : enter_(r, enter)
        , exit_(r, exit)
    {
    }

private:
    struct _enter
    {
        _enter(_resourceT& r, _lambdaT f) { f(r); }
    };
    struct _exit
    {
        _exit(_resourceT& r, _lambdaT f)
            : r_(r)
            , f_(f)
        {
        }
        ~_exit(void) { f_(r_); }

    private:
        _lambdaT f_;
        _resourceT& r_;
    };
    _enter enter_;
    _exit exit_;
};

}  // namespace Orc

constexpr auto MAX_GUID_STRLEN = 40;

#pragma managed(pop)
