#include <spdlog/details/log_msg.h>
#include <string>
#include <vector>
#include <spdlog/sinks/base_sink.h>
#include <deque>

namespace Orc::Log {

struct RawLogEntry
{
    spdlog::level::level_enum level;
    spdlog::log_clock::time_point time;
    size_t thread_id;
    std::string payload;  // The "unformatted" message with arguments resolved

    RawLogEntry(const spdlog::details::log_msg& msg)
        : level(msg.level)
        , time(msg.time)
        , thread_id(msg.thread_id)
        , payload(msg.payload.data(), msg.payload.size())
    {
    }
};

template <typename Mutex>
class BacktraceSink final : public spdlog::sinks::base_sink<Mutex>
{
public:
    explicit BacktraceSink(size_t max_items)
        : m_maxItems(max_items)
    {
    }

    void dump_to(spdlog::logger& target_logger)
    {
        std::lock_guard<Mutex> lock(this->mutex_);
        if (m_buffer.empty())
        {
            return;
        }

        struct SinkSettings
        {
            spdlog::sinks::sink* ptr;
            spdlog::level::level_enum level;
        };
        std::vector<SinkSettings> originalSettings;
        originalSettings.reserve(target_logger.sinks().size());

        for (auto& sink : target_logger.sinks())
        {
            originalSettings.push_back({sink.get(), sink->level()});
            sink->set_level(spdlog::level::trace);
        }

        // Helper to send a raw string to all sinks (except self)
        auto send_internal = [&](const std::string& text, spdlog::level::level_enum lvl) {
            spdlog::details::log_msg msg(target_logger.name(), lvl, text);
            for (auto& sink : target_logger.sinks())
            {
                if (sink.get() != this)
                {
                    sink->log(msg);
                }
            }
        };

        send_internal("--- BACKTRACE START ---", spdlog::level::info);

        for (const auto& entry : m_buffer)
        {
            spdlog::details::log_msg msg(
                entry.time, spdlog::source_loc {}, target_logger.name(), entry.level, entry.payload);
            msg.thread_id = entry.thread_id;

            for (auto& sink : target_logger.sinks())
            {
                if (sink.get() != this)
                {
                    sink->log(msg);
                }
            }
        }

        send_internal("--- BACKTRACE END ---", spdlog::level::info);

        for (const auto& setting : originalSettings)
        {
            setting.ptr->set_level(setting.level);
        }

        m_buffer.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (m_buffer.size() >= m_maxItems)
        {
            m_buffer.pop_front();
        }

        m_buffer.emplace_back(msg);
    }

    void flush_() override {}

private:
    size_t m_maxItems;
    std::deque<RawLogEntry> m_buffer;
};

}  // namespace Orc::Log
