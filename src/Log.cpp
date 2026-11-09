#include "Log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <cstdlib>
#include <string>

namespace kaflip::log {

namespace {
constexpr int kAreaCount = 8;

const char* area_name(Area a) {
    switch (a) {
        case Area::App:      return "app";
        case Area::Shell:    return "shell";
        case Area::Registry: return "registry";
        case Area::Recents:  return "recents";
        case Area::Io:       return "io";
        case Area::Book:     return "book";   // s002: the open EPUB + chapter moves
        case Area::Web:      return "web";    // s002: the WebKit view + kaflip-book: scheme
        case Area::Library:  return "library";   // s006: the shelf, scans, covers
    }
    return "?";
}

std::array<std::shared_ptr<spdlog::logger>, kAreaCount>& loggers() {
    static std::array<std::shared_ptr<spdlog::logger>, kAreaCount> a{};
    return a;
}
}  // namespace

void init() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto& a = loggers();
    for (int i = 0; i < kAreaCount; ++i) {
        const auto area = static_cast<Area>(i);
        auto lg = std::make_shared<spdlog::logger>(area_name(area), sink);
        lg->set_level(spdlog::level::info);   // default INFO; raise one area to TRACE to debug
        a[static_cast<std::size_t>(i)] = lg;
    }
    // s003: KAFLIP_DEBUG=book,web (or "all") turns those areas to DEBUG at
    // launch -- log mode without a rebuild.
    if (const char* env = std::getenv("KAFLIP_DEBUG")) {
        const std::string want = std::string(",") + env + ",";
        for (int i = 0; i < kAreaCount; ++i) {
            const std::string name = area_name(static_cast<Area>(i));
            if (want.find(",all,") != std::string::npos || want.find("," + name + ",") != std::string::npos)
                a[static_cast<std::size_t>(i)]->set_level(spdlog::level::debug);
        }
    }
}

std::shared_ptr<spdlog::logger> get(Area area) {
    return loggers()[static_cast<std::size_t>(area)];
}

void set_level(Area area, spdlog::level::level_enum level) {
    if (auto lg = get(area)) lg->set_level(level);
}

}  // namespace kaflip::log
