// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#ifndef QLEVER_SRC_UTIL_METRICS_LOGGING_H
#define QLEVER_SRC_UTIL_METRICS_LOGGING_H

#include <opentelemetry/logs/logger.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/version.h>

#include <memory>

#include "util/Log.h"

// Forward declaration, so that this header does not have to pull in the OTEL
// logs SDK. OTEL uses custom macros for its namespaces.
OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk::logs {
class LoggerProvider;
}
OPENTELEMETRY_END_NAMESPACE

namespace ad_utility::logging {

// Sets up the export of QLever's log messages via OpenTelemetry. On destruction
// the export is stopped and the buffered records are flushed.
class [[nodiscard(
    "Log messages are only exported while this handle is "
    "alive.")]] LoggingHandle {
  // Empty when the export is disabled. The SDK type rather than the API type,
  // because only the former can be shut down.
  std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> provider_;
  // Todo<qup42> do we want the whole shenanigans with restoring and pointe
  // The sink installed in `Log.h`. Owned here, because `setLogSink` only takes
  // a non-owning pointer.
  std::unique_ptr<LogSink> sink_;
  // Restored on shutdown, so that the handle is transparent for whatever was
  // installed before.
  LogSink* previousSink_ = nullptr;

 public:
  LoggingHandle() = default;
  LoggingHandle(
      std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> provider,
      std::unique_ptr<LogSink> sink);
  ~LoggingHandle();

  LoggingHandle(LoggingHandle&&) noexcept;
  LoggingHandle& operator=(LoggingHandle&&) noexcept;
  LoggingHandle(const LoggingHandle&) = delete;
  LoggingHandle& operator=(const LoggingHandle&) = delete;

  void shutdown();
};

// Sets up the export of log messages susing the standard
// `OTEL_EXPORTER_OTLP_LOGS_*` environment variables. Messages filtering is the
// same as for stdout (so both compile and runtime level apply). When the
// returned `LoggingHandle` is dropped, only the textual log remains.
[[nodiscard]] LoggingHandle initialize();

// A `LogSink` that turns QLever's log messages into OTEL log records and emits
// them through `logger`.
std::unique_ptr<LogSink> makeOtelLogSink(
    opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> logger);

}  // namespace ad_utility::logging

#endif  // QLEVER_SRC_UTIL_METRICS_LOGGING_H
