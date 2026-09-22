/**
 * @file   larcoreobj/LoggingUtil/Logging.h
 * @brief  Logging facility built on the spdlog default logger.
 *
 * Replacement for `messagefacility`. Every message is emitted through the spdlog
 * *default* logger and is prefixed with the fully scoped class and method (or
 * function) name of its origin, derived at compile time.
 *
 * Usage:
 *
 *     LAR_LOG_ERROR << "channel " << c << " maps to no wire";
 *     // -> geo::WireReadoutGeom::ChannelsIntersect: channel 7 maps to no wire
 *
 * A message may also be accumulated across statements; it is emitted when the
 * object is destroyed:
 *
 *     auto log = LAR_LOG_INFO;
 *     log << "tests completed:";
 *     for (auto const& t: tests) log << "\n  " << t;
 *
 * @note This header lives in `larcoreobj` because it is the only package that
 *       `larcorealg`, `lardataobj` and `lardataalg` all already depend on.
 *       See spdlog-migration/OPEN_QUESTION_ANSWERS.md.
 */

#ifndef LARCOREOBJ_LOGGINGUTIL_LOGGING_H
#define LARCOREOBJ_LOGGINGUTIL_LOGGING_H

#include "spdlog/spdlog.h"

#include <ios>       // std::ios_base, for the std::setw/std::fixed overload
#include <ostream>   // std::ostream, for the std::endl/std::flush overload
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace lar::log {

  namespace detail {

    /**
     * @brief Extracts `ns::Class::method` from a `__PRETTY_FUNCTION__` expansion.
     *
     * Evaluated at compile time, so suppressed messages cost no runtime string work.
     */
    constexpr std::string_view qualifiedName(std::string_view pretty)
    {
      // drop the " [with T = ...]" suffix GCC appends for templates
      if (auto const b = pretty.find(" [with "); b != std::string_view::npos)
        pretty.remove_suffix(pretty.size() - b);

      // find the '(' opening the parameter list, skipping any nested parentheses
      std::size_t depth = 0;
      std::size_t paren = std::string_view::npos;
      for (std::size_t i = pretty.size(); i-- > 0;) {
        char const c = pretty[i];
        if (c == ')')
          ++depth;
        else if (c == '(') {
          if (--depth == 0) {
            paren = i;
            break;
          }
        }
      }
      if (paren == std::string_view::npos) return pretty;

      // walk back to the space separating the return type, ignoring <> contents
      std::string_view const head = pretty.substr(0, paren);
      int angle = 0;
      for (std::size_t i = head.size(); i-- > 0;) {
        char const c = head[i];
        if (c == '>')
          ++angle;
        else if (c == '<')
          --angle;
        else if (c == ' ' && angle == 0)
          return head.substr(i + 1);
      }
      return head;
    }

  } // namespace detail

  /**
   * @brief Accumulates a message via `operator<<` and emits it on destruction.
   *
   * Satisfies the `Stream&&` protocol used by the `Print*Info()` family and by the
   * `lar::dump::` manipulators, so it can be passed wherever a `std::ostream` was
   * previously expected.
   */
  class LogStream {

    /*
     * LOAD-BEARING: this must remain a std::ostringstream.
     *
     * Several categories of code reach the logger only because streaming into it
     * ultimately reduces to `std::ostream&`:
     *   - types whose only stream support is a non-template, ostream-only ADL
     *     `operator<<` (e.g. util::quantities and friends);
     *   - generic manipulators declared
     *     `template <typename Stream> Stream& operator<<(Stream&&, X&&)`;
     *   - every ordinary ostream-insertable type that has no fmt::formatter.
     *
     * Replacing this with an fmt::memory_buffer (or any non-ostream sink) breaks
     * all of them at once, and does so at dozens of call sites simultaneously.
     */
    std::ostringstream buf_;
    std::string_view where_;
    spdlog::level::level_enum level_;
    bool active_;

  public:
    LogStream(spdlog::level::level_enum level, std::string_view where)
      : where_{where}, level_{level}, active_{spdlog::should_log(level)}
    {}

    /**
     * @brief Move constructor: transfers the buffer and disarms the source.
     *
     * REQUIRED. A class declaring a destructor gets no implicitly generated move
     * constructor, so without this the type cannot be stored or returned by value
     * at all -- and dumper helpers do store the stream by value in a member.
     *
     * The source is disarmed so the message is emitted exactly once, by whichever
     * object survives.
     */
    LogStream(LogStream&& other)
      : buf_{std::move(other.buf_)}
      , where_{other.where_}
      , level_{other.level_}
      , active_{other.active_}
    {
      other.active_ = false;
    }

    LogStream(LogStream const&) = delete;
    LogStream& operator=(LogStream const&) = delete;
    LogStream& operator=(LogStream&&) = delete; // messagefacility deletes this too

    ~LogStream()
    {
      if (!active_) return;
      auto const body = buf_.str();
      if (body.empty()) return; // nothing was streamed: emit nothing
      /*
       * The body is an ARGUMENT, never a format string. Passing it as the format
       * string makes any message containing a literal '{' fail at RUNTIME with
       * "invalid format string" -- and dump manipulators routinely emit
       * "{ 0; 0; 0 }". This is not a style preference.
       */
      if (where_.empty())
        spdlog::log(level_, "{}", body);
      else
        spdlog::log(level_, "{}: {}", where_, body);
    }

    /// Whether the message will actually be emitted.
    /// @note Explicit on purpose. Do not write `if (LAR_LOG_TRACE)`: that
    ///       constructs a temporary. Use lar::log::debugEnabled() instead.
    explicit operator bool() const noexcept { return active_; }

    template <typename T>
    LogStream& operator<<(T const& value)
    {
      if (active_) buf_ << value;
      return *this;
    }

    /// Supports `std::endl`, `std::flush`, ...
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&))
    {
      if (active_) buf_ << manip;
      return *this;
    }

    /// Supports `std::setw`, `std::setprecision`, `std::fixed`, ...
    /// @note These manipulators take `std::ios_base&`, NOT `std::ostream&`, so
    ///       this second overload is required in addition to the one above.
    LogStream& operator<<(std::ios_base& (*manip)(std::ios_base&))
    {
      if (active_) buf_ << manip;
      return *this;
    }

  }; // class LogStream

  /// Equivalent of `mf::isDebugEnabled()`.
  inline bool debugEnabled()
  {
    return spdlog::should_log(spdlog::level::debug);
  }

  /**
   * @brief Sets up the default logger.
   *
   * Optional -- spdlog lazily creates a stdout default logger, so logging works
   * without ever calling this, which matters for messages emitted during static
   * destruction or teardown.
   *
   * Callers that relied on a verbose messagefacility threshold MUST call this with
   * the matching level: spdlog defaults to `info`, so `trace`/`debug` sites would
   * otherwise vanish with no compile error and no test failure.
   *
   * @param appName    application name (currently unused by the bare `%v` pattern)
   * @param level      runtime level threshold
   * @param logFile    if non-empty, also write to this file (second sink)
   */
  void setup(std::string const& appName = "lar",
             spdlog::level::level_enum level = spdlog::level::info,
             std::string const& logFile = {});

} // namespace lar::log

// __PRETTY_FUNCTION__ is a GCC/Clang extension; __func__ is the standard fallback
// but carries no class scope.
#if defined(__GNUC__) || defined(__clang__)
#define LAR_LOG_WHERE_ (::lar::log::detail::qualifiedName(__PRETTY_FUNCTION__))
#else
#define LAR_LOG_WHERE_ (::std::string_view{__func__})
#endif

#define LAR_LOG(level) ::lar::log::LogStream((level), LAR_LOG_WHERE_)

#define LAR_LOG_ERROR LAR_LOG(::spdlog::level::err)
#define LAR_LOG_WARNING LAR_LOG(::spdlog::level::warn)
#define LAR_LOG_INFO LAR_LOG(::spdlog::level::info)
#define LAR_LOG_DEBUG LAR_LOG(::spdlog::level::debug)
#define LAR_LOG_TRACE LAR_LOG(::spdlog::level::trace)

#endif // LARCOREOBJ_LOGGINGUTIL_LOGGING_H
