/**
 * @file   larcoreobj/LoggingUtil/Logging.cxx
 * @brief  Implementation of the logging setup helper.
 * @see    larcoreobj/LoggingUtil/Logging.h
 */

#include "larcoreobj/LoggingUtil/Logging.h"

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <memory>
#include <vector>

namespace lar::log {

  void setup(std::string const& appName,
             spdlog::level::level_enum level,
             std::string const& logFile)
  {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    // Old messagefacility configurations frequently declared a file destination
    // alongside the console one; that is retained here as a second sink.
    if (!logFile.empty())
      sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true));

    auto logger =
      std::make_shared<spdlog::logger>(appName, sinks.begin(), sinks.end());

    // Bare message text: no timestamp, no level, no logger name. The scoped-name
    // prefix supplied by the LAR_LOG_* macros carries the origin, and keeping the
    // line bare makes before/after output diffing tractable during migration.
    logger->set_pattern("%v");

    // Set explicitly on both the logger and the sinks: a sink whose own level is
    // higher than the logger's would silently drop records.
    logger->set_level(level);
    for (auto& sink : logger->sinks())
      sink->set_level(level);

    // flush_on is required for the error path: a message accumulated and emitted
    // during stack unwinding must reach the sink before the process dies.
    logger->flush_on(spdlog::level::err);

    spdlog::set_default_logger(std::move(logger));

    // The free functions spdlog::log()/should_log() consult this as well as the
    // default logger's own level.
    spdlog::set_level(level);
  }

} // namespace lar::log
