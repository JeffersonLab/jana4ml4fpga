#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <JANA/JApplication.h>
#include <JANA/JService.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

/**
 * Centralized spdlog service with ASPECT-based loggers.
 *
 * Instead of one logger per factory/component (which proved to be an
 * anti-pattern: hundreds of loggers and hundreds of *:LogLevel parameters),
 * the application logs through a small fixed set of aspect loggers:
 *
 *   evio - readout: file/TCP event sources, mmap reader, EVIO parser
 *   gem  - GEM reconstruction: adapter service/factory, calibration lifecycle
 *   out  - persistence: flat-tree writer, ROOT file service, merging
 *   dqm  - data-quality monitoring / histogramming
 *
 * Levels are configured per aspect:   -P<aspect>:log=debug
 * and globally (the default level):   -Plog:level=info
 * Recognized level names: trace, debug, info, warn, error, critical, off.
 *
 * Components fetch their aspect logger once in Init/Open:
 *   m_log = app->GetService<Log_service>()->logger("gem");
 */
class Log_service : public JService {
public:
    explicit Log_service(JApplication* app);

    /// Get (and cache) the logger for an aspect. Unknown aspect names are
    /// allowed (a logger is created on the fly with its own <name>:log param),
    /// but prefer the documented aspects above.
    std::shared_ptr<spdlog::logger> logger(const std::string& aspect);

    /// The global default level (from -Plog:level)
    spdlog::level::level_enum getDefaultLevel();
    std::string getDefaultLevelStr();

private:
    std::recursive_mutex m_lock;
    JApplication* m_application;
    std::string m_log_level_str = "info";
};
