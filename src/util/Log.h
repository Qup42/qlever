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
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>

#include "backports/keywords.h"
#include "util/EnumWithStrings.h"
#include "util/Forward.h"
#include "util/TypeTraits.h"

// The numeric values of the log levels, and the compile-time log level
// `QLEVER_COMPILETIME_LOGLEVEL`. These are macros with plain integer values
// (and not C++ constants), because they have to be usable in `#if` directives
// (see `Timer.h` for an example) and because `QLEVER_COMPILETIME_LOGLEVEL` is
// set by the build system via `-DQLEVER_COMPILETIME_LOGLEVEL=...`.
//
// NOTE: The values have to be kept in sync with the `LOG_LEVEL_...` variables
// in `CMakeLists.txt`, which translate the user-facing `LOGLEVEL=<NAME>` CMake
// option (for example `-DLOGLEVEL=INFO`) into the corresponding number.
//
// NOTE: It is important that the values are integer literals and not
// identifiers. A macro that expands to an identifier (as the former
// `#define LOGLEVEL DEBUG` did) has to be resolved by name lookup at each of
// its expansion sites, which breaks as soon as any dependency declares or
// defines a conflicting `DEBUG`. Even worse, in a `#if` directive an
// identifier that is not a macro silently evaluates to `0`, so `#if LOGLEVEL
// >= TIMING` was always true, no matter what the log level actually was.
#define QLEVER_FATAL 0
#define QLEVER_ERROR 1
#define QLEVER_WARN 2
#define QLEVER_INFO 3
#define QLEVER_DEBUG 4
#define QLEVER_TIMING 5
#define QLEVER_TRACE 6

#ifndef QLEVER_COMPILETIME_LOGLEVEL
#define QLEVER_COMPILETIME_LOGLEVEL QLEVER_DEBUG
#endif

namespace ad_utility {

namespace detail {
enum class LogLevelEnum {
  FATAL = QLEVER_FATAL,
  ERROR = QLEVER_ERROR,
  WARN = QLEVER_WARN,
  INFO = QLEVER_INFO,
  DEBUG = QLEVER_DEBUG,
  TIMING = QLEVER_TIMING,
  TRACE = QLEVER_TRACE
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

// The compile-time log level, as a typed constant. Use this (and not the
// `QLEVER_COMPILETIME_LOGLEVEL` macro) everywhere where a macro is not
// strictly required, that is, everywhere outside of `#if` directives.
inline constexpr LogLevel::Enum compileTimeLogLevel =
    static_cast<LogLevel::Enum>(QLEVER_COMPILETIME_LOGLEVEL);

}  // namespace ad_utility

// Global type alias so that `LogLevel::Enum::FATAL` etc. can be written
// without the `ad_utility::` prefix.
//
// NOTE: Deliberately no `using enum LogLevel::Enum;` here. That would inject
// the names `FATAL`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TIMING` and `TRACE`
// into the global namespace of every translation unit that (transitively)
// includes this header, which are exactly the identifiers that other
// libraries and platform SDKs like to declare or `#define` themselves. Always
// spell the log levels out as `ad_utility::LogLevel::Enum::DEBUG` etc.
using LogLevel = ad_utility::LogLevel;

// The branching logger: both the compile-time level (`compileTimeLogLevel`) and
// the runtime level must pass for a message to be logged. Nothing after the
// `<<` is evaluated for a suppressed message, which makes this variant
// efficient, but also introduces a branch at every single call site, which is
// unfriendly to coverage measurements. The `LogStreamProxy` temporary is held
// for the entire
// `<<` chain and destroyed at the semicolon that ends the statement.
#define AD_LOG_BRANCHING(x)                        \
  if (!::ad_utility::detail::logLevelIsEnabled(x)) \
    ;                                              \
  else                                             \
    ::ad_utility::getLogStream(x, __FILE__, __LINE__)  // NOLINT

// The branchless logger: a plain function call that always returns a stream
// (see `ad_utility::getLogStream`). For a suppressed message that stream
// discards its input, so the arguments after the `<<` are always evaluated
// (which is less efficient), but the call site contains no branch at all
// (which is friendly to coverage measurements, as the single branch lives in
// this header instead of in each of the hundreds of call sites).
#define AD_LOG_BRANCHLESS(x) ::ad_utility::getLogStream(x, __FILE__, __LINE__)

// The logger that is actually used by the `AD_LOG_...` macros below. This is
// the only place where the choice between the two styles above is made; it is
// controlled by the `BRANCHLESS_LOGGING` CMake option.
//
// NOTE: Always log via one of the `AD_LOG_<LEVEL>` macros below. Use
// `AD_LOG_IMPL` directly only for the rare case that the log level is not
// known at compile time (a grep for `AD_LOG_IMPL` outside of this header
// lists all such places).
#ifdef QLEVER_BRANCHLESS_LOGGING
#define AD_LOG_IMPL(x) AD_LOG_BRANCHLESS(x)
#else
#define AD_LOG_IMPL(x) AD_LOG_BRANCHING(x)
#endif

// Macros for the different log levels.
#define AD_LOG_FATAL AD_LOG_IMPL(::ad_utility::LogLevel::Enum::FATAL)
#define AD_LOG_ERROR AD_LOG_IMPL(::ad_utility::LogLevel::Enum::ERROR)
#define AD_LOG_WARN AD_LOG_IMPL(::ad_utility::LogLevel::Enum::WARN)
#define AD_LOG_INFO AD_LOG_IMPL(::ad_utility::LogLevel::Enum::INFO)
#define AD_LOG_DEBUG AD_LOG_IMPL(::ad_utility::LogLevel::Enum::DEBUG)
#define AD_LOG_TIMING AD_LOG_IMPL(::ad_utility::LogLevel::Enum::TIMING)
#define AD_LOG_TRACE AD_LOG_IMPL(::ad_utility::LogLevel::Enum::TRACE)

namespace ad_utility {

namespace detail {
// Global mutex to ensure log messages from different threads are not
// interleaved (acquired by the `LogStreamProxy` that both loggers return).
inline std::mutex logMutex;

static constexpr LogLevel::Enum defaultLogLevel =
    std::min(compileTimeLogLevel, LogLevel::Enum::INFO);
// Runtime log level; messages with a higher level than this are suppressed.
// Defaults to the less verbose of INFO and `compileTimeLogLevel`, so that the
// runtime level is never set to something the binary cannot log.
inline std::atomic<LogLevel::Enum> runtimeLogLevel = defaultLogLevel;
// A stream that discards everything that is written to it. It is created from
// a null `streambuf`, so it is in a `bad` state from the start and every
// insertion into it is a cheap no-op. It is used for messages that are
// suppressed by the compile-time or the runtime log level.
inline std::ostream& nullStream() {
  static std::ostream stream{nullptr};
  return stream;
}

// Return true if a message with the given `level` has to be logged, according
// to the compile-time (`compileTimeLogLevel`) and the runtime log level.
inline bool logLevelIsEnabled(LogLevel::Enum level) {
  return level <= compileTimeLogLevel &&
         level <= runtimeLogLevel.load(std::memory_order_relaxed);
}
}  // namespace detail

// Set the runtime log level. Throws if `level` is more verbose than the
// `compileTimeLogLevel`, because such messages are compiled out and can
// never appear regardless of the runtime setting.
inline void setRuntimeLogLevel(LogLevel level) {
  if (level.value() > compileTimeLogLevel) {
    throw std::runtime_error{
        absl::StrCat("Cannot set runtime log level to `", level.toString(),
                     "` because the compile-time log level is `",
                     LogLevel{compileTimeLogLevel}.toString(),
                     "`. Recompile with -DLOGLEVEL=", level.toString(),
                     " or higher to enable this log level.")};
  }
  detail::runtimeLogLevel.store(level.value(), std::memory_order_relaxed);
}

// Get the runtime log level (see `setRuntimeLogLevel`). Note: The relaxed
// memory order is deliberate and consistent with the other accesses to
// `detail::runtimeLogLevel` (see `setRuntimeLogLevel` and
// `detail::logLevelIsEnabled`): the log level is a standalone value that
// synchronizes nothing, and the accesses are on the hot path of every single
// log statement.
inline LogLevel getRuntimeLogLevel() {
  return detail::runtimeLogLevel.load(std::memory_order_relaxed);
}

// While an object of this class is alive, the runtime log level is the given
// `level` (or `compileTimeLogLevel`, if that is less verbose); the
// previous level is restored when the object is destroyed. Use this to silence
// a subroutine that logs more than the caller wants, or to set up a specific
// log level in a test.
//
// NOTE: The runtime log level is global, so this must only be used when nothing
// else logs concurrently, for example in a standalone command-line tool or in a
// test, but never in the server.
class QL_NODISCARD(
    "The log level is only changed while this object is alive. Store it in a "
    "variable.") ScopedLogLevel {
 private:
  LogLevel previousLevel_ = getRuntimeLogLevel();

 public:
  explicit ScopedLogLevel(LogLevel::Enum level) {
    setRuntimeLogLevel(std::min(level, compileTimeLogLevel));
  }

  ScopedLogLevel(const ScopedLogLevel&) = delete;
  ScopedLogLevel& operator=(const ScopedLogLevel&) = delete;

  // Restore the previous level via the raw store: it was the runtime log
  // level before and hence is always valid, and the checking
  // `setRuntimeLogLevel` could structurally throw, which a destructor must
  // never do.
  ~ScopedLogLevel() {
    detail::runtimeLogLevel.store(previousLevel_.value(),
                                  std::memory_order_relaxed);
  }
};

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
  // Write the prefix (timestamp and log level) of a single log message to the
  // global logging stream and return that stream. Note: The caller has to hold
  // the `detail::logMutex` while calling this and while writing the message
  // itself, see the `LogStreamProxy` class.
  static std::ostream& getLog(LogLevel::Enum level, absl::Time timestamp) {
    // Use the singleton logging stream as target.
    return LogstreamChoice::get().getStream()
           << formatTimestamp(timestamp) << " - " << LogLevel{level}.toString()
           << ": ";
  }

  // Overload that uses the current time as the timestamp.
  static std::ostream& getLog(LogLevel::Enum level) {
    return getLog(level, absl::Now());
  }

  // Imbue the stream that is currently used for logging with the given
  // `locale`, for example to print large numbers with thousands separators.
  // Note: This has to be called again after the logging stream was changed via
  // `setGlobalLoggingStream`, as the locale is a property of the stream.
  static void imbue(const std::locale& locale) {
    LogstreamChoice::get().getStream().imbue(locale);
  }

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
// The currently installed sink, or `nullptr` if there is none. Read at most
// once per log message, so a plain `atomic` is enough.
inline std::atomic<LogSink*> logSink = nullptr;

// True while a message is being handed to the sink. A sink that logs itself,
// directly or indirectly, would otherwise recurse infinitely; such nested
// messages only go to the textual log.
inline thread_local bool insideLogSink = false;
}  // namespace detail

// Install `sink` as an additional destination for log messages and return the
// previously installed one (`nullptr` if there was none). Pass `nullptr` to
// uninstall. The caller keeps the ownership of the sink and has to uninstall it
// before destroying it.
inline LogSink* setLogSink(LogSink* sink) {
  return detail::logSink.exchange(sink, std::memory_order_acq_rel);
}

namespace detail {
// A `streambuf` that forwards everything that is written to it to another
// `streambuf` and additionally appends it to a string. This way the message of
// a log statement still goes to the textual log immediately (in particular, it
// is not lost if the process dies in the middle of the statement), and is at
// the same time available as a whole once the statement is complete.
class TeeStreambuf : public std::streambuf {
 private:
  std::streambuf* target_;
  std::string captured_;

 public:
  explicit TeeStreambuf(std::streambuf* target) : target_{target} {}

  // Everything that was written so far.
  const std::string& captured() const { return captured_; }

 protected:
  int_type overflow(int_type c) override {
    if (traits_type::eq_int_type(c, traits_type::eof())) {
      return traits_type::not_eof(c);
    }
    auto character = traits_type::to_char_type(c);
    captured_.push_back(character);
    return target_->sputc(character);
  }

  std::streamsize xsputn(const char* s, std::streamsize count) override {
    captured_.append(s, static_cast<size_t>(count));
    return target_->sputn(s, count);
  }

  int sync() override { return target_->pubsync(); }
};

// The additional state that a log statement needs while a `LogSink` is
// installed. The message is written to `stream()`, which tees it into the
// textual log and into a buffer, from which it is handed to the sink when the
// statement is complete.
class SinkCapture {
 private:
  LogSink* sink_;
  TeeStreambuf buffer_;
  std::ostream stream_;
  LogLevel level_;
  absl::Time timestamp_;
  const char* file_;
  int line_;

 public:
  SinkCapture(LogSink* sink, std::ostream& target, LogLevel::Enum level,
              absl::Time timestamp, const char* file, int line)
      : sink_{sink},
        buffer_{target.rdbuf()},
        stream_{&buffer_},
        level_{level},
        timestamp_{timestamp},
        file_{file},
        line_{line} {
    // The message has to be formatted exactly as in the textual log, in
    // particular with the locale that was set via `Log::imbue`.
    stream_.imbue(target.getloc());
  }

  SinkCapture(const SinkCapture&) = delete;
  SinkCapture& operator=(const SinkCapture&) = delete;

  // The stream that the `<<` chain of the log statement writes to.
  std::ostream& stream() { return stream_; }

  // Hand the complete message to the sink. Called from the destructor of the
  // `LogStreamProxy`, which must not throw. Note that `terminateIfThrows` from
  // `util/ExceptionHandling.h` cannot be used here, because that header itself
  // logs.
  void emit() noexcept {
    try {
      std::string_view message = buffer_.captured();
      // Messages ending in `\r` are in-place redraws of a `ProgressBar` on the
      // terminal rather than log events of their own.
      if (!message.empty() && message.back() == '\r') {
        return;
      }
      // The `LogRecord` holds the message without the trailing newline.
      if (!message.empty() && message.back() == '\n') {
        message.remove_suffix(1);
      }
      insideLogSink = true;
      absl::Cleanup resetGuard{[]() { insideLogSink = false; }};
      sink_->emit(LogRecord{level_, timestamp_, message, file_, line_});
    } catch (...) {
    }
  }
};
}  // namespace detail

// The stream-like object that is returned by both loggers (see the
// `AD_LOG_BRANCHING` and `AD_LOG_BRANCHLESS` macros). It holds a reference to
// the stream that the message is written to and, if the message is actually
// logged, the global log mutex. As it is returned by value, the temporary lives
// until the end of the full expression, so the mutex is held for the complete
// `<<` chain. For a suppressed message, the mutex is not acquired, so the
// arguments of a suppressed message may safely log or take other locks, even
// though the branchless logger evaluates them.
class LogStreamProxy {
 private:
  std::unique_lock<std::mutex> lock_;
  std::ostream* stream_ = &detail::nullStream();
  // Only present while a `LogSink` is installed. Handed the complete message
  // by the destructor below.
  std::optional<detail::SinkCapture> capture_;

 public:
  // If a message with the given `level` has to be logged, acquire the global
  // log mutex and write the prefix of the message. Otherwise, the message is
  // written to the null stream, which discards it.
  //
  // NOTE: The level is checked here even though the branching logger has
  // already checked it at the call site. That check is a single relaxed load,
  // and it only happens for messages that are logged anyway, which then do I/O
  // under a mutex.
  explicit LogStreamProxy(LogLevel::Enum level, const char* file, int line) {
    if (!detail::logLevelIsEnabled(level)) {
      return;
    }
    lock_ = std::unique_lock{detail::logMutex};
    auto timestamp = absl::Now();
    stream_ = &Log::getLog(level, timestamp);
    auto* sink = detail::logSink.load(std::memory_order_acquire);
    if (sink != nullptr && !detail::insideLogSink) {
      capture_.emplace(sink, *stream_, level, timestamp, file, line);
      stream_ = &capture_.value().stream();
    }
  }

  LogStreamProxy(const LogStreamProxy&) = delete;
  LogStreamProxy& operator=(const LogStreamProxy&) = delete;

  // Hand the complete message to the installed `LogSink`, if any.
  ~LogStreamProxy() {
    if (capture_.has_value()) {
      // Release the global log mutex first. A sink may well log itself, and
      // the `insideLogSink` guard only breaks the infinite recursion, not the
      // deadlock on the (non-recursive) mutex.
      if (lock_.owns_lock()) {
        lock_.unlock();
      }
      capture_.value().emit();
    }
  }

  // Write `arg` to the underlying stream. The result is the stream itself, so
  // that the remaining arguments of the `<<` chain bypass this proxy.
  template <typename T>
  friend std::ostream& operator<<(const LogStreamProxy& proxy, T&& arg) {
    return *proxy.stream_ << AD_FWD(arg);
  }

  // Overload for stream manipulators like `std::endl`, for which the template
  // argument of the overload above cannot be deduced.
  friend std::ostream& operator<<(const LogStreamProxy& proxy,
                                  std::ostream& (*manipulator)(std::ostream&)) {
    return *proxy.stream_ << manipulator;
  }
};

// The implementation of both logger macros: always return a stream, which
// discards the message if it is suppressed by the compile-time or the runtime
// log level. Note: The `LogStreamProxy` is neither copyable nor movable,
// returning it by value works because of the guaranteed copy elision for
// prvalues.
inline LogStreamProxy getLogStream(LogLevel::Enum level, const char* file,
                                   int line) {
  return LogStreamProxy{level, file, line};
}
}  // namespace ad_utility

#endif  // QLEVER_SRC_UTIL_LOG_H
