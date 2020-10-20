#pragma once

namespace Orc::StructuredOutput::JSON {

template <class _RapidWriter>
inline Writer<_RapidWriter>::Writer(
    logger pLog,
    std::shared_ptr<ByteStream> stream,
    std::unique_ptr<Options>&& pOptions)
    : StructuredOutputWriter(std::move(pLog), std::move(pOptions))
    , m_Stream(std::move(stream))
    , rapidWriter(m_Stream)
{
}

}  // namespace Orc::StructuredOutput::JSON
