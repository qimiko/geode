#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <vector>
#include <fstream>
#include <string>

namespace geode::log {
    class Logger {
    private:
        std::vector<Log> m_logs;
        std::ofstream m_logStream;

        Logger() = default;
    public:
        static Logger* get();

        void setup();

        void push(Severity sev, std::string&& thread, std::string&& source, int32_t nestCount,
            std::string&& content);

        std::vector<Log> const& list();
        void clear();
    };

    class Nest::Impl {
    public:
        int32_t m_nestLevel;
        int32_t m_nestCountOffset;
        Impl(int32_t nestLevel, int32_t nestCountOffset);
    };
}
