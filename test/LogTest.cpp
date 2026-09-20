// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Johannes Kalmbach <kalmbach@cs.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#include <absl/cleanup/cleanup.h>
#include <absl/strings/str_split.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ctre.hpp>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

#include "./util/GTestHelpers.h"
#include "backports/algorithm.h"
#include "util/Log.h"
#include "util/jthread.h"

// _____________________________________________________________________________
TEST(LogTest, TypeName) { EXPECT_EQ(LogLevel::typeName(), "log level"); }

// _____________________________________________________________________________
TEST(LogTest, StringConversions) {
  EXPECT_EQ(LogLevel::fromString("FATAL"), LogLevel{LogLevel::Enum::FATAL});
  EXPECT_EQ(LogLevel::fromString("ERROR"), LogLevel{LogLevel::Enum::ERROR});
  EXPECT_EQ(LogLevel::fromString("WARN"), LogLevel{LogLevel::Enum::WARN});
  EXPECT_EQ(LogLevel::fromString("INFO"), LogLevel{LogLevel::Enum::INFO});
  EXPECT_EQ(LogLevel::fromString("DEBUG"), LogLevel{LogLevel::Enum::DEBUG});
  EXPECT_EQ(LogLevel::fromString("TIMING"), LogLevel{LogLevel::Enum::TIMING});
  EXPECT_EQ(LogLevel::fromString("TRACE"), LogLevel{LogLevel::Enum::TRACE});

  EXPECT_EQ(LogLevel{LogLevel::Enum::FATAL}.toString(), "FATAL");
  EXPECT_EQ(LogLevel{LogLevel::Enum::INFO}.toString(), "INFO");
  EXPECT_EQ(LogLevel{LogLevel::Enum::TRACE}.toString(), "TRACE");

  EXPECT_THROW(LogLevel::fromString("INVALID"), std::runtime_error);
}

// _____________________________________________________________________________
TEST(LogTest, SetRuntimeLogLevel) {
  // Setting to INFO requires a compile-time log level of at least INFO;
  // skip otherwise.
  ENFORCE_LOG_LEVEL_OR_SKIP(INFO);
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  EXPECT_EQ(ad_utility::detail::runtimeLogLevel.load(), LogLevel::Enum::FATAL);

  // Setting to INFO must succeed (ENFORCE_LOG_LEVEL_OR_SKIP(INFO) guards this).
  ad_utility::setRuntimeLogLevel(LogLevel::Enum::INFO);
  EXPECT_EQ(ad_utility::detail::runtimeLogLevel.load(), LogLevel::Enum::INFO);
}

// _____________________________________________________________________________
TEST(LogTest, ExceptionOnTooVerboseLevel) {
  // If the compile-time log level is already TRACE, every runtime level is
  // valid — there is nothing to throw, so we skip.
  if constexpr (ad_utility::compileTimeLogLevel >= LogLevel::Enum::TRACE) {
    GTEST_SKIP() << "The compile-time log level is already TRACE; no "
                    "more-verbose level exists.";
  } else {
    constexpr auto tooVerbose = static_cast<LogLevel::Enum>(
        static_cast<int>(ad_utility::compileTimeLogLevel) + 1);
    AD_EXPECT_THROW_WITH_MESSAGE(
        ad_utility::setRuntimeLogLevel(LogLevel{tooVerbose}),
        ::testing::HasSubstr("compile-time log level"));
  }
}

// _____________________________________________________________________________
// `Log::imbue` affects the stream that is currently used for logging, and not
// hardcodedly `std::cout`.
TEST(LogTest, Imbue) {
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  // Without an imbued locale, large numbers are printed without separators.
  AD_LOG_FATAL << 1234567 << '\n';
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("1234567"));

  // After imbuing the locale with the thousands separators, the same number is
  // grouped. Note: The locale is only imbued into the string stream that is
  // currently used for logging, so this doesn't affect any other test.
  ss.str({});
  ad_utility::Log::imbue(ad_utility::commaLocale);
  AD_LOG_FATAL << 1234567 << '\n';
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("1,234,567"));
}

namespace {

// Write a single log argument to `stream`. Invocable arguments are invoked and
// their result is written instead, see `lazyLogArgs` below.
template <typename T>
void streamLogArg(std::ostream& stream, const T& arg) {
  if constexpr (std::is_invocable_v<const T&>) {
    stream << arg();
  } else {
    stream << arg;
  }
}

// A group of log arguments that is only written when it is actually inserted
// into a log stream. Arguments that are invocable are invoked at that point,
// which makes it observable whether a logger evaluates the arguments of a
// suppressed message at all. Only use this as a temporary inside a log
// statement, as it stores references to its arguments.
template <typename... Args>
class LazyLogArgs {
 private:
  std::tuple<const Args&...> args_;

 public:
  explicit LazyLogArgs(const Args&... args) : args_{args...} {}

  friend std::ostream& operator<<(std::ostream& stream,
                                  const LazyLogArgs& lazyArgs) {
    std::apply(
        [&stream](const auto&... args) { (streamLogArg(stream, args), ...); },
        lazyArgs.args_);
    return stream;
  }
};

// Deduce the template arguments of `LazyLogArgs`, see there for details.
template <typename... Args>
LazyLogArgs<Args...> lazyLogArgs(const Args&... args) {
  return LazyLogArgs<Args...>{args...};
}

// The `AD_LOG_...` macros have two implementations (see `util/Log.h`), of which
// only one is active in a given build. The following three function objects
// make all of them accessible at the same time, such that the typed tests below
// run for each of them, independently of the build configuration. Each of them
// logs `args...` at the given `level`, and knows whether it evaluates the
// arguments of a suppressed message.
struct BranchingLogger {
  static constexpr std::string_view name_ = "Branching";
  static constexpr bool evaluatesArgumentsOfSuppressedMessage_ = false;

  template <typename... Args>
  void operator()(LogLevel::Enum level, const Args&... args) const {
    AD_LOG_BRANCHING(level) << lazyLogArgs(args...);
  }

  // Log messages that start with a stream manipulator. Note: The `std::endl`
  // has to appear literally inside the macro, because it is a template and thus
  // requires a special overload of the `operator<<` of the `LogStreamProxy`.
  void logManipulators(LogLevel::Enum level) const {
    AD_LOG_BRANCHING(level) << std::endl;
    AD_LOG_BRANCHING(level) << std::setw(4) << 42 << "\n";
  }
};

struct BranchlessLogger {
  static constexpr std::string_view name_ = "Branchless";
  static constexpr bool evaluatesArgumentsOfSuppressedMessage_ = true;

  template <typename... Args>
  void operator()(LogLevel::Enum level, const Args&... args) const {
    AD_LOG_BRANCHLESS(level) << lazyLogArgs(args...);
  }

  // See `BranchingLogger::logManipulators`.
  void logManipulators(LogLevel::Enum level) const {
    AD_LOG_BRANCHLESS(level) << std::endl;
    AD_LOG_BRANCHLESS(level) << std::setw(4) << 42 << "\n";
  }
};

// The logger that the `AD_LOG_...` macros actually dispatch to, which depends
// on whether `QLEVER_BRANCHLESS_LOGGING` is defined. Testing it makes sure that
// the dispatch works in either configuration.
struct DefaultLogger {
  static constexpr std::string_view name_ = "Default";
  static constexpr bool evaluatesArgumentsOfSuppressedMessage_ =
#ifdef QLEVER_BRANCHLESS_LOGGING
      true;
#else
      false;
#endif

  template <typename... Args>
  void operator()(LogLevel::Enum level, const Args&... args) const {
    AD_LOG_IMPL(level) << lazyLogArgs(args...);
  }

  // See `BranchingLogger::logManipulators`.
  void logManipulators(LogLevel::Enum level) const {
    AD_LOG_IMPL(level) << std::endl;
    AD_LOG_IMPL(level) << std::setw(4) << 42 << "\n";
  }
};

// The fixture for the tests that are run for each of the loggers above.
template <typename Logger>
class LogTestTyped : public ::testing::Test {};

using Loggers =
    ::testing::Types<BranchingLogger, BranchlessLogger, DefaultLogger>;

// Use the name of the logger instead of its index in the names of the typed
// tests.
struct LoggerName {
  template <typename Logger>
  static std::string GetName(int) {
    return std::string{Logger::name_};
  }
};
}  // namespace

TYPED_TEST_SUITE(LogTestTyped, Loggers, LoggerName);

// _____________________________________________________________________________
// A message with a level that passes the runtime log level is logged (including
// the prefix with the timestamp and the log level), a message with a more
// verbose level is completely suppressed.
TYPED_TEST(LogTestTyped, StreamFiltering) {
  // FATAL (0) always passes the compile-time guards. ERROR (1) is suppressed at
  // runtime when the level is set to FATAL.
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  TypeParam{}(LogLevel::Enum::FATAL, "hello-fatal");
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("FATAL: hello-fatal"));

  ss.str({});
  TypeParam{}(LogLevel::Enum::ERROR, "hello-error");
  EXPECT_THAT(ss.str(), ::testing::IsEmpty());
}

// _____________________________________________________________________________
// Log messages that consist of several arguments of different types are written
// correctly, and a suppressed message produces no output at all.
TYPED_TEST(LogTestTyped, MultipleArguments) {
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  TypeParam{}(LogLevel::Enum::FATAL, "a=", 42, " b=", std::string{"str"}, '!');
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("a=42 b=str!"));

  ss.str({});
  TypeParam{}(LogLevel::Enum::ERROR, "a=", 42, " b=", std::string{"str"}, '!');
  EXPECT_THAT(ss.str(), ::testing::IsEmpty());
}

// _____________________________________________________________________________
// The arguments of a message that is actually logged are always evaluated, no
// matter which of the loggers is used.
TYPED_TEST(LogTestTyped, ArgumentsOfLoggedMessageAreEvaluated) {
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  size_t numEvaluations = 0;
  auto expensiveArgument = [&numEvaluations] {
    ++numEvaluations;
    return "expensive";
  };
  TypeParam{}(LogLevel::Enum::FATAL, "value: ", expensiveArgument);
  EXPECT_EQ(numEvaluations, 1);
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("value: expensive"));
}

// _____________________________________________________________________________
// The arguments of a suppressed message are evaluated by the branchless logger
// (which then discards the output), but not by the branching logger, which
// doesn't evaluate them at all.
TYPED_TEST(LogTestTyped, ArgumentsOfSuppressedMessage) {
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  size_t numEvaluations = 0;
  auto expensiveArgument = [&numEvaluations] {
    ++numEvaluations;
    return "expensive";
  };
  TypeParam{}(LogLevel::Enum::ERROR, "value: ", expensiveArgument);
  EXPECT_THAT(ss.str(), ::testing::IsEmpty());
  EXPECT_EQ(numEvaluations,
            TypeParam::evaluatesArgumentsOfSuppressedMessage_ ? 1u : 0u);
}

// _____________________________________________________________________________
// An argument of a suppressed message may itself log (or take other locks),
// because the global log mutex is not held while the arguments of a suppressed
// message are evaluated. Without this guarantee, the branchless logger would
// deadlock here.
TYPED_TEST(LogTestTyped, ArgumentsOfSuppressedMessageMayLog) {
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  auto loggingArgument = [] {
    AD_LOG_FATAL << "from-inside";
    return "outer";
  };
  TypeParam{}(LogLevel::Enum::ERROR, "value: ", loggingArgument);
  EXPECT_THAT(ss.str(), ::testing::Not(::testing::HasSubstr("outer")));
  if constexpr (TypeParam::evaluatesArgumentsOfSuppressedMessage_) {
    EXPECT_THAT(ss.str(), ::testing::HasSubstr("FATAL: from-inside"));
  } else {
    EXPECT_THAT(ss.str(), ::testing::IsEmpty());
  }
}

// _____________________________________________________________________________
// Messages that start with a stream manipulator are logged correctly, and are
// suppressed if their level doesn't pass the runtime log level.
TYPED_TEST(LogTestTyped, Manipulators) {
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  TypeParam{}.logManipulators(LogLevel::Enum::FATAL);
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("  42"));
  // Both of the logged messages end with a newline.
  EXPECT_EQ(ql::ranges::count(ss.str(), '\n'), 2);

  ss.str({});
  TypeParam{}.logManipulators(LogLevel::Enum::ERROR);
  EXPECT_THAT(ss.str(), ::testing::IsEmpty());
}

// _____________________________________________________________________________
// Concurrently log many messages, each of them consisting of several `<<`
// arguments, and check that no two messages are interleaved.
TYPED_TEST(LogTestTyped, ThreadSafety) {
  static constexpr size_t numThreads = 8;
  static constexpr size_t msgsPerThread = 200;

  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();

  // Spawn N threads; each logs M lines using multiple `<<` operators. The
  // multi-part write is intentional: if the mutex were absent, partial writes
  // from different threads could interleave within a single line.
  {
    std::vector<ad_utility::JThread> threads;
    threads.reserve(numThreads);
    for (size_t t = 0; t < numThreads; ++t) {
      threads.emplace_back([t] {
        for (size_t m = 0; m < msgsPerThread; ++m) {
          TypeParam{}(LogLevel::Enum::FATAL, "Thread ", t, " message ", m,
                      " end\n");
        }
      });
    }
  }  // All `JThread`s join here on destruction.

  // Pre-populate every expected (thread, msg) pair. Each parsed line must match
  // the log-line prefix pattern, and its pair must still be in the set (first
  // occurrence); the pair is then removed. Any interleaved write would produce
  // a line that fails the regex, and a duplicate would fail the contains check.
  static constexpr ctll::fixed_string kPattern{
      R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} - FATAL: Thread (?<thread>\d+) message (?<msg>\d+) end)"};
  std::set<std::pair<size_t, size_t>> expected;
  for (size_t t = 0; t < numThreads; ++t) {
    for (size_t m = 0; m < msgsPerThread; ++m) {
      expected.emplace(t, m);
    }
  }
  for (auto line : absl::StrSplit(ss.str(), '\n', absl::SkipEmpty())) {
    auto match = ctre::match<kPattern>(line);
    ASSERT_TRUE(match) << "Line does not match expected log format: " << line;
    // Note: The `template` keywords are needed because the type of `match` is
    // dependent inside the body of a typed test.
    auto pair = std::make_pair(
        match.template get<"thread">().template to_number<size_t>(),
        match.template get<"msg">().template to_number<size_t>());
    ASSERT_TRUE(expected.contains(pair))
        << "Unexpected or duplicate: thread=" << pair.first
        << " msg=" << pair.second;
    expected.erase(pair);
  }
  EXPECT_TRUE(expected.empty());
}

// _____________________________________________________________________________
TEST(LogTest, GetRuntimeLogLevel) {
  ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
  EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::FATAL);
  EXPECT_EQ(ad_utility::getRuntimeLogLevel(),
            ad_utility::detail::runtimeLogLevel.load());
}

// _____________________________________________________________________________
TEST(LogTest, ScopedLogLevelSetsAndRestoresLevel) {
  // The `ENFORCE_LOG_LEVEL_OR_SKIP` also guarantees the restoration of the log
  // level if one of the assertions below fails and the scope is left early.
  ENFORCE_LOG_LEVEL_OR_SKIP(WARN);
  {
    ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
    EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::FATAL);
  }
  EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::WARN);
}

// _____________________________________________________________________________
TEST(LogTest, ScopedLogLevelRestoresLevelOnException) {
  ENFORCE_LOG_LEVEL_OR_SKIP(WARN);
  auto throwFromInsideTheScope = [] {
    ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
    EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::FATAL);
    throw std::runtime_error{"Thrown inside the scope."};
  };
  EXPECT_THROW(throwFromInsideTheScope(), std::runtime_error);
  EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::WARN);
}

// _____________________________________________________________________________
TEST(LogTest, ScopedLogLevelIsNested) {
  ENFORCE_LOG_LEVEL_OR_SKIP(WARN);
  {
    ad_utility::ScopedLogLevel outer{LogLevel::Enum::ERROR};
    EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::ERROR);
    {
      ad_utility::ScopedLogLevel inner{LogLevel::Enum::FATAL};
      EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::FATAL);
    }
    // The inner object restores the level that the outer object has set.
    EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::ERROR);
  }
  EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::WARN);
}

// _____________________________________________________________________________
TEST(LogTest, ScopedLogLevelClampsToCompileTimeLogLevel) {
  // If the compile-time log level is already `TRACE`, then there is no more
  // verbose level that could be clamped.
  if constexpr (ad_utility::compileTimeLogLevel >= LogLevel::Enum::TRACE) {
    GTEST_SKIP() << "The compile-time log level is already TRACE; no "
                    "more-verbose level exists.";
  } else {
    constexpr auto tooVerbose = static_cast<LogLevel::Enum>(
        static_cast<int>(ad_utility::compileTimeLogLevel) + 1);
    ad_utility::ScopedLogLevel outerLogLevel{LogLevel::Enum::FATAL};
    {
      // Requesting a more verbose level than the compile-time log level must
      // neither throw nor exceed that log level.
      ad_utility::ScopedLogLevel scopedLogLevel{tooVerbose};
      EXPECT_EQ(ad_utility::getRuntimeLogLevel(),
                ad_utility::compileTimeLogLevel);
    }
    EXPECT_EQ(ad_utility::getRuntimeLogLevel(), LogLevel::Enum::FATAL);
  }
}

// _____________________________________________________________________________
TEST(LogTest, ScopedLogLevelSuppressesLogOutput) {
  ENFORCE_LOG_LEVEL_OR_SKIP(ERROR);
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();
  {
    ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
    AD_LOG_ERROR << "hello-scoped-error";
    EXPECT_THAT(ss.str(),
                ::testing::Not(::testing::HasSubstr("hello-scoped-error")));
    // A message at the scoped level itself is still logged.
    AD_LOG_FATAL << "hello-scoped-fatal";
    EXPECT_THAT(ss.str(), ::testing::HasSubstr("hello-scoped-fatal"));
  }
  // Outside of the scope, the message is logged again.
  AD_LOG_ERROR << "hello-scoped-error";
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("hello-scoped-error"));
}

namespace {

// A `LogSink` that stores copies of the records it receives. Copies are
// necessary because the string views of a `LogRecord` are only valid for the
// duration of the `emit` call.
class CollectingSink : public ad_utility::LogSink {
 public:
  struct Record {
    LogLevel level_;
    absl::Time timestamp_;
    std::string message_;
    std::string file_;
    int line_;
  };
  std::vector<Record> records_;

  void emit(const ad_utility::LogRecord& record) override {
    records_.push_back(Record{record.level_, record.timestamp_,
                              std::string{record.message_},
                              std::string{record.file_}, record.line_});
  }
};

// Install `sink` for the lifetime of the returned object and restore the
// previously installed sink afterwards. A sink must never be destroyed while
// it is installed, so all tests below have to use this.
[[nodiscard]] auto scopedLogSink(ad_utility::LogSink* sink) {
  auto* previous = ad_utility::setLogSink(sink);
  return absl::Cleanup{[previous]() { ad_utility::setLogSink(previous); }};
}

}  // namespace

// _____________________________________________________________________________
// An installed `LogSink` gets the message without the prefix and without the
// trailing newline, together with the metadata of the log statement.
TEST(LogTest, LogSinkReceivesMessages) {
  ENFORCE_LOG_LEVEL_OR_SKIP(ERROR);
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();
  CollectingSink sink;
  auto before = absl::Now();
  {
    auto sinkCleanup = scopedLogSink(&sink);
    AD_LOG_ERROR << "hello " << 42 << std::endl;
  }
  const int expectedLine = __LINE__ - 2;

  ASSERT_EQ(sink.records_.size(), 1);
  const auto& record = sink.records_.at(0);
  EXPECT_EQ(record.message_, "hello 42");
  EXPECT_EQ(record.level_, LogLevel{LogLevel::Enum::ERROR});
  EXPECT_THAT(record.file_, ::testing::EndsWith("LogTest.cpp"));
  EXPECT_EQ(record.line_, expectedLine);
  EXPECT_GE(record.timestamp_, before);
  EXPECT_LE(record.timestamp_, absl::Now());

  // The message still goes to the textual log, with the very same timestamp.
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("hello 42\n"));
  EXPECT_THAT(ss.str(), ::testing::HasSubstr(ad_utility::Log::formatTimestamp(
                            record.timestamp_)));
}

// _____________________________________________________________________________
// `setLogSink` returns the previously installed sink, and after uninstalling a
// sink it no longer receives messages.
TEST(LogTest, SetLogSinkReturnsThePreviousSink) {
  ENFORCE_LOG_LEVEL_OR_SKIP(ERROR);
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();
  CollectingSink first;
  CollectingSink second;
  {
    auto firstCleanup = scopedLogSink(&first);
    {
      auto secondCleanup = scopedLogSink(&second);
      AD_LOG_ERROR << "to-the-second-sink" << std::endl;
    }
    AD_LOG_ERROR << "to-the-first-sink" << std::endl;
  }
  AD_LOG_ERROR << "to-no-sink" << std::endl;

  ASSERT_EQ(first.records_.size(), 1);
  EXPECT_EQ(first.records_.at(0).message_, "to-the-first-sink");
  ASSERT_EQ(second.records_.size(), 1);
  EXPECT_EQ(second.records_.at(0).message_, "to-the-second-sink");
  // The textual log is unaffected by the (un)installing of the sinks.
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("to-no-sink"));
}

// _____________________________________________________________________________
// Messages that are suppressed by the runtime log level never reach the sink.
TEST(LogTest, LogSinkDoesNotSeeSuppressedMessages) {
  ENFORCE_LOG_LEVEL_OR_SKIP(ERROR);
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();
  CollectingSink sink;
  {
    auto sinkCleanup = scopedLogSink(&sink);
    ad_utility::ScopedLogLevel scopedLogLevel{LogLevel::Enum::FATAL};
    AD_LOG_ERROR << "suppressed" << std::endl;
    AD_LOG_FATAL << "not-suppressed" << std::endl;
  }

  ASSERT_EQ(sink.records_.size(), 1);
  EXPECT_EQ(sink.records_.at(0).message_, "not-suppressed");
}

// _____________________________________________________________________________
// Messages that end in `\r` are in-place redraws of a `ProgressBar` on the
// terminal, not log events of their own, and hence are not handed to the sink.
TEST(LogTest, LogSinkIgnoresProgressBarRedraws) {
  ENFORCE_LOG_LEVEL_OR_SKIP(ERROR);
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();
  CollectingSink sink;
  {
    auto sinkCleanup = scopedLogSink(&sink);
    AD_LOG_ERROR << "progress 50%\r";
    AD_LOG_ERROR << "progress 100%" << std::endl;
  }

  ASSERT_EQ(sink.records_.size(), 1);
  EXPECT_EQ(sink.records_.at(0).message_, "progress 100%");
  // Both messages are still written to the textual log.
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("progress 50%\r"));
}

// _____________________________________________________________________________
// A sink that logs itself neither deadlocks on the global log mutex nor
// recurses infinitely; the nested message only goes to the textual log.
TEST(LogTest, LogSinkMayLogItself) {
  ENFORCE_LOG_LEVEL_OR_SKIP(ERROR);
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();
  class LoggingSink : public ad_utility::LogSink {
   public:
    int numCalls_ = 0;
    void emit(const ad_utility::LogRecord&) override {
      ++numCalls_;
      AD_LOG_ERROR << "from-inside-the-sink" << std::endl;
    }
  };
  LoggingSink sink;
  {
    auto sinkCleanup = scopedLogSink(&sink);
    AD_LOG_ERROR << "trigger" << std::endl;
  }

  EXPECT_EQ(sink.numCalls_, 1);
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("from-inside-the-sink"));
}

// _____________________________________________________________________________
// The message that the sink sees is formatted exactly as in the textual log,
// in particular with the locale that was set via `Log::imbue`.
TEST(LogTest, LogSinkUsesTheLocaleOfTheLogStream) {
  ENFORCE_LOG_LEVEL_OR_SKIP(ERROR);
  auto [streamCleanup, ss] = setGlobalLoggingStreamToStringStream();
  // Note: The locale is only imbued into the string stream that is currently
  // used for logging, so this doesn't affect any other test.
  ad_utility::Log::imbue(ad_utility::commaLocale);
  CollectingSink sink;
  {
    auto sinkCleanup = scopedLogSink(&sink);
    AD_LOG_ERROR << 1234567 << std::endl;
  }

  ASSERT_EQ(sink.records_.size(), 1);
  EXPECT_EQ(sink.records_.at(0).message_, "1,234,567");
  EXPECT_THAT(ss.str(), ::testing::HasSubstr("1,234,567"));
}
