// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#include "util/metrics/Logging.h"

#include <absl/time/time.h>
#include <opentelemetry/common/timestamp.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_options.h>
#include <opentelemetry/sdk/logs/logger_provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/semconv/code_attributes.h>

#include <utility>

#include "util/Exception.h"
#include "util/metrics/Resource.h"

namespace nostd = opentelemetry::nostd;
namespace logs_api = opentelemetry::logs;
namespace logs_sdk = opentelemetry::sdk::logs;
namespace otel_common = opentelemetry::common;
namespace semconv = opentelemetry::semconv;

namespace ad_utility::logging {
namespace {

// Convert to the string view type of the OTEL API. Note that this has to keep
// the size: a `std::string_view` is not necessarily null-terminated, so passing
// its `data()` as a `const char*` attribute value would read out of bounds.
nostd::string_view toOtel(std::string_view view) {
  return {view.data(), view.size()};
}

logs_api::Severity toOtelSeverity(LogLevel level) {
  switch (level.value()) {
    case LogLevel::Enum::FATAL:
      return logs_api::Severity::kFatal;
    case LogLevel::Enum::ERROR:
      return logs_api::Severity::kError;
    case LogLevel::Enum::WARN:
      return logs_api::Severity::kWarn;
    case LogLevel::Enum::INFO:
      return logs_api::Severity::kInfo;
    case LogLevel::Enum::DEBUG:
      return logs_api::Severity::kDebug;
    case LogLevel::Enum::TIMING:
      // Timing has no direct OTEL counterpart. There are values between the
      // defined levels. Chose a level between trace and debug.
      return logs_api::Severity::kTrace2;
    case LogLevel::Enum::TRACE:
      return logs_api::Severity::kTrace;
  }
  AD_FAIL();
}

// The `LogSink` that bridges `AD_LOG_...` to OTEL.
//
// NOTE: The records carry no trace or span id, because the association
// mechanism (thread local storage) does not work with async and multiple
// threads.
class OtelLogSink : public LogSink {
  nostd::shared_ptr<logs_api::Logger> logger_;

 public:
  explicit OtelLogSink(nostd::shared_ptr<logs_api::Logger> logger)
      : logger_{std::move(logger)} {}

  void emit(const LogRecord& record) override {
    auto otelRecord = logger_->CreateLogRecord();
    AD_CONTRACT_CHECK(otelRecord != nullptr);
    otelRecord->SetTimestamp(
        otel_common::SystemTimestamp{absl::ToChronoTime(record.timestamp_)});
    otelRecord->SetSeverity(toOtelSeverity(record.level_));
    otelRecord->SetBody(toOtel(record.message_));
    otelRecord->SetAttribute("qlever.log_level",
                             toOtel(record.level_.toString()));
    otelRecord->SetAttribute(semconv::code::kCodeFilePath,
                             toOtel(record.file_));
    otelRecord->SetAttribute(semconv::code::kCodeLineNumber, record.line_);
    logger_->EmitLogRecord(std::move(otelRecord));
  }
};
}  // namespace

// _____________________________________________________________________________
std::unique_ptr<LogSink> makeOtelLogSink(
    nostd::shared_ptr<logs_api::Logger> logger) {
  return std::make_unique<OtelLogSink>(std::move(logger));
}

// _____________________________________________________________________________
LoggingHandle::LoggingHandle(std::shared_ptr<logs_sdk::LoggerProvider> provider,
                             std::unique_ptr<LogSink> sink)
    : provider_{std::move(provider)}, sink_{std::move(sink)} {
  previousSink_ = setLogSink(sink_.get());
}

// _____________________________________________________________________________
LoggingHandle::LoggingHandle(LoggingHandle&& other) noexcept
    : provider_{std::move(other.provider_)},
      sink_{std::move(other.sink_)},
      previousSink_{other.previousSink_} {
  other.previousSink_ = nullptr;
}

// _____________________________________________________________________________
LoggingHandle& LoggingHandle::operator=(LoggingHandle&& other) noexcept {
  if (this != &other) {
    shutdown();
    provider_ = std::move(other.provider_);
    sink_ = std::move(other.sink_);
    previousSink_ = other.previousSink_;
    other.previousSink_ = nullptr;
  }
  return *this;
}

// _____________________________________________________________________________
LoggingHandle::~LoggingHandle() { shutdown(); }

// _____________________________________________________________________________
void LoggingHandle::shutdown() {
  if (provider_ == nullptr) {
    return;
  }
  setLogSink(previousSink_);
  previousSink_ = nullptr;
  sink_ = nullptr;
  logs_api::Provider::SetLoggerProvider(
      nostd::shared_ptr<logs_api::LoggerProvider>{});
  // The batch processor buffers records and exports them from a background
  // thread, so without this the messages of the last few seconds would be lost.
  provider_->Shutdown();
  provider_ = nullptr;
}

// _____________________________________________________________________________
LoggingHandle initialize() {
  // The endpoint is configured with the standard `OTEL_EXPORTER_OTLP_LOGS_*`
  // variables.
  auto exporter =
      opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create();
  auto provider = logs_sdk::LoggerProviderFactory::Create(
      logs_sdk::BatchLogRecordProcessorFactory::Create(
          std::move(exporter), logs_sdk::BatchLogRecordProcessorOptions{}),
      metrics::sharedResource());

  auto sharedProvider = std::shared_ptr{std::move(provider)};
  logs_api::Provider::SetLoggerProvider(
      nostd::shared_ptr<logs_api::LoggerProvider>{sharedProvider});

  // Logs, unlike other signals, has no single argument `GetLogger`.
  auto sink = makeOtelLogSink(sharedProvider->GetLogger("qlever", "qlever"));
  return LoggingHandle{std::move(sharedProvider), std::move(sink)};
}

}  // namespace ad_utility::logging
