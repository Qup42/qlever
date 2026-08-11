// Copyright 2011 - 2024, University of Freiburg
// Chair of Algorithms and Data Structures
// Authors: Björn Buchhold <buchhold@cs.uni-freiburg.de> [2011 - 2014]
//          Johannes Kalmbach <bast@cs.uni-freiburg.de>
//          Hannah Bast <bast@cs.uni-freiburg.de>

#ifndef QLEVER_SRC_UTIL_LOG_H
#define QLEVER_SRC_UTIL_LOG_H

#include <absl/cleanup/cleanup.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include "backports/keywords.h"
#include "util/EnumWithStrings.h"
#include "util/TypeTraits.h"

#ifndef LOGLEVEL
#define LOGLEVEL DEBUG
#endif

namespace ad_utility {

namespace detail {
enum class LogLevelEnum {
  FATAL = 0,
  ERROR = 1,
  WARN = 2,
  INFO = 3,
  DEBUG = 4,
  TIMING = 5,
  TRACE = 6
};
}

// Log level wrapper using the `EnumWithStrings` CRTP base to provide string
// conversion, JSON serialization, and boost::program_options integration.
class LogLevel : public EnumWithStrings<LogLevel, detail::LogLevelEnum> {
 public:
  using Enum = detail::LogLevelEnum;
  static constexpr std::array<std::pair<Enum, std::string_view>, 7>
      descriptions_{{{Enum::FATAL, "FATAL"},
                     {Enum::ERROR, "ERROR"},
                     {Enum::WARN, "WARN"},
                     {Enum::INFO, "INFO"},
                     {Enum::DEBUG, "DEBUG"},
                     {Enum::TIMING, "TIMING"},
                     {Enum::TRACE, "TRACE"}}};
  static constexpr std::string_view typeName() { return "log level"; }
  using EnumWithStrings::EnumWithStrings;
};

}  // namespace ad_utility

// Global type alias and using-enum so that `LogLevel::FATAL` etc. and the
// compile-time `LOGLEVEL` macro keep working outside `namespace ad_utility`.
using LogLevel = ad_utility::LogLevel;
using enum LogLevel::Enum;

// Both the compile-time level (LOGLEVEL) and the runtime level must pass for a
// message to be logged. If they don't, the `LogStatement` is never constructed
// and the arguments of the `<<` chain are never evaluated. The `LogStatement`
// temporary lives until the semicolon that ends the statement, so its
// destructor is the signal that the message is complete.
#define AD_LOG(x)                                                     \
  if (x > LOGLEVEL || x > ::ad_utility::detail::runtimeLogLevel.load( \
                              std::memory_order_relaxed))             \
    ;                                                                 \
  else                                                                \
    ::ad_utility::detail::LogStatement{x, __FILE__, __LINE__}         \
        .stream()  // NOLINT

// Macros for the different log levels.
#define AD_LOG_FATAL AD_LOG(LogLevel::Enum::FATAL)
#define AD_LOG_ERROR AD_LOG(LogLevel::Enum::ERROR)
#define AD_LOG_WARN AD_LOG(LogLevel::Enum::WARN)
#define AD_LOG_INFO AD_LOG(LogLevel::Enum::INFO)
#define AD_LOG_DEBUG AD_LOG(LogLevel::Enum::DEBUG)
#define AD_LOG_TIMING AD_LOG(LogLevel::Enum::TIMING)
#define AD_LOG_TRACE AD_LOG(LogLevel::Enum::TRACE)

namespace ad_utility {

namespace detail {
// Global mutex to ensure log messages from different threads are not
// interleaved (acquired via the comma-operator trick in the AD_LOG macro).
inline std::mutex logMutex;

static constexpr LogLevel::Enum defaultLogLevel =
    std::min(LOGLEVEL, LogLevel::Enum::INFO);
// Runtime log level; messages with a higher level than this are suppressed.
// Defaults to the less verbose of INFO and the compile-time LOGLEVEL so that
// the runtime level is never set to something the binary cannot log.
inline std::atomic<LogLevel::Enum> runtimeLogLevel = defaultLogLevel;
}  // namespace detail

// Set the runtime log level. Throws if `level` is more verbose than the
// compile-time LOGLEVEL, because such messages are compiled out and can never
// appear regardless of the runtime setting.
inline void setRuntimeLogLevel(LogLevel level) {
  if (level.value() > LOGLEVEL) {
    throw std::runtime_error{absl::StrCat(
        "Cannot set runtime log level to `", level.toString(),
        "` because the compile-time log level is `",
        LogLevel{LOGLEVEL}.toString(), "`. Recompile with -DLOGLEVEL=",
        level.toString(), " or higher to enable this log level.")};
  }
  detail::runtimeLogLevel.store(level.value(), std::memory_order_relaxed);
}

// A singleton that holds a pointer to a single `std::ostream`. This enables us
// to globally redirect the `AD_LOG_...` macros to another output stream.
struct LogstreamChoice {
  std::ostream& getStream() { return *_stream; }
  void setStream(std::ostream* stream) { _stream = stream; }

  static LogstreamChoice& get() {
    static LogstreamChoice s;
    return s;
  }  // instance
  LogstreamChoice(const LogstreamChoice&) = delete;
  LogstreamChoice& operator=(const LogstreamChoice&) = delete;

 private:
  LogstreamChoice() {}
  ~LogstreamChoice() {}

  // default to cout since it was the default before
  std::ostream* _stream = &std::cout;
};

// After this call, every use of `AD_LOG_...` will use the specified stream.
// In tests use `setGlobalLoggingStreamForTesting` from `GTestHelpers.h` which
// also restores the previous value.
inline void setGlobalLoggingStream(std::ostream* stream) {
  LogstreamChoice::get().setStream(stream);
}

// Helper class to get thousandth separators in a locale
class CommaNumPunct : public std::numpunct<char> {
 protected:
  virtual char do_thousands_sep() const { return ','; }

  virtual std::string do_grouping() const { return "\03"; }
};

const static std::locale commaLocale(std::locale(), new CommaNumPunct());

// The class that actually does the logging.
class Log {
 public:
  template <LogLevel::Enum LEVEL>
  static std::ostream& getLog() {
    // use the singleton logging stream as target.
    return LogstreamChoice::get().getStream()
           << getTimeStamp() << " - " << LogLevel{LEVEL}.toString() << ": ";
  }

  static void imbue(const std::locale& locale) { std::cout.imbue(locale); }

  static std::string formatTimestamp(absl::Time time) {
    return absl::FormatTime("%Y-%m-%d %H:%M:%E3S", time, absl::LocalTimeZone());
  }

  static std::string getTimeStamp() { return formatTimestamp(absl::Now()); }
};

// One complete log message, as handed to a `LogSink`. The string views point
// into storage owned by the caller and are only valid for the duration of the
// `emit` call.
struct LogRecord {
  LogLevel level_;
  absl::Time timestamp_;
  // The message without the `<timestamp> - <LEVEL>: ` prefix and without a
  // trailing newline.
  std::string_view message_;
  std::string_view file_;
  int line_;
};

// An additional destination for log messages, besides the textual log stream.
class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void emit(const LogRecord& record) = 0;
};

namespace detail {
// The currently installed sink, or `nullptr` if there is none. Read once per
// log message, so an `atomic` is enoug.
inline std::atomic<LogSink*> logSink = nullptr;

// Guards against a sink that logs itself. // TODO: this bbool and th ee
// rationale are fishy
inline thread_local bool insideLogSink = false;
}  // namespace detail

// Install `sink` as the additional destination for log messages and return the
// previously installed one. Pass `nullptr` to uninstall. The caller keeps
// ownership of the sink and has to uninstall it before destroying it.
inline LogSink* setLogSink(LogSink* sink) {
  return detail::logSink.exchange(sink, std::memory_order_acq_rel);
}

namespace detail {
// A single log statement. Buffers the `<<` chain and, on destruction, writes
// the formatted line to the global log stream and hands the message to the
// installed `LogSink`, if any. The writing of the message to the global log
// stream is protected by a mutex.
class LogStatement {
  LogLevel level_;
  absl::Time timestamp_ = absl::Now();
  const char* file_;
  int line_;
  std::ostringstream buffer_;

 public:
  LogStatement(LogLevel::Enum level, const char* file, int line)
      : level_{level}, file_{file}, line_{line} {
    // Set the locale settings for the buffer.
    buffer_.imbue(LogstreamChoice::get().getStream().getloc());
  }

  LogStatement(const LogStatement&) = delete;
  LogStatement& operator=(const LogStatement&) = delete;

  // The stream that the `<<` chain of the log statement writes to.
  std::ostream& stream() { return buffer_; }

  ~LogStatement() {
    // A destructor must not throw. Note that `terminateIfThrows` from
    // `util/ExceptionHandling.h` cannot be used here, because that header
    // itself logs.
    try {
      writeMessage();
    } catch (...) {
    }
  }

 private:
  void writeMessage() {
    std::string message = std::move(buffer_).str();
    {
      std::lock_guard lock{logMutex};
      auto& stream = LogstreamChoice::get().getStream();
      stream << Log::formatTimestamp(timestamp_) << " - " << level_.toString()
             << ": " << message;
      // The `std::endl` and `std::flush` at the call sites used to reach the
      // output stream directly, but now only act on the buffer.
      // TODO<qup42> this changes the behaviour to always flush.
      stream.flush();
    }
    emitToSink(message);
  }

  void emitToSink(std::string_view message) {
    auto* sink = logSink.load(std::memory_order_acquire);
    if (sink == nullptr || insideLogSink) {
      return;
    }
    // Messages ending in `\r` are in-place redraws of a `ProgressBar` on the
    // terminal rather than log events of their own.
    if (!message.empty() && message.back() == '\r') {
      return;
    }
    if (!message.empty() && message.back() == '\n') {
      message.remove_suffix(1);
    }
    insideLogSink = true;
    absl::Cleanup resetGuard{[]() { insideLogSink = false; }};
    sink->emit(LogRecord{level_, timestamp_, message, file_, line_});
  }
};
}  // namespace detail
}  // namespace ad_utility

#endif  // QLEVER_SRC_UTIL_LOG_H
