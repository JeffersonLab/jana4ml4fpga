#include "Log_service.h"

#include <JANA/JException.h>
#include <extensions/spdlog/SpdlogExtensions.h>
#include <spdlog/sinks/stdout_color_sinks.h>

Log_service::Log_service(JApplication* app) {
    m_application = app;

    // Global default level for all aspect loggers
    m_application->SetDefaultParameter("log:level", m_log_level_str,
                                       "Default log level: trace, debug, info, warn, error, critical, off");
    spdlog::default_logger()->set_level(spdlog::extensions::ParseLogLevel(m_log_level_str));
}

std::shared_ptr<spdlog::logger> Log_service::logger(const std::string& aspect) {
    try {
        std::lock_guard<std::recursive_mutex> locker(m_lock);

        auto logger = spdlog::get(aspect);
        if (!logger) {
            logger = spdlog::default_logger()->clone(aspect);

            // Per-aspect level override, e.g. -Pgem:log=debug
            std::string log_level_str = m_log_level_str;
            m_application->SetDefaultParameter(
                aspect + ":log", log_level_str,
                "Log level for '" + aspect + "': trace, debug, info, warn, error, critical, off");
            logger->set_level(spdlog::extensions::ParseLogLevel(log_level_str));
        }
        return logger;
    }
    catch (const std::exception& exception) {
        throw JException(exception.what());
    }
}

spdlog::level::level_enum Log_service::getDefaultLevel() { return spdlog::default_logger()->level(); }

std::string Log_service::getDefaultLevelStr() {
    return spdlog::extensions::LogLevelToString(getDefaultLevel());
}
