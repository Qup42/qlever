// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#include "util/metrics/Tracing.h"

#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/sdk/common/env_variables.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/samplers/always_on_factory.h>
#include <opentelemetry/sdk/trace/samplers/parent_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/semconv/error_attributes.h>
#include <opentelemetry/semconv/exception_attributes.h>
#include <opentelemetry/trace/propagation/http_trace_context.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/span_metadata.h>

#include <string>
#include <utility>

#include "util/Log.h"
#include "util/metrics/Resource.h"

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace otel_common = opentelemetry::sdk::common;
namespace semconv = opentelemetry::semconv;

namespace ad_utility::tracing {
namespace {

// The name under which our spans are grouped. Matches the meter name used in
// `ServerMetrics.cpp`.
constexpr const char* TRACER_NAME = "qlever";

// Convert to the string view type of the OTEL API. Note that this has to keep
// the size: a `std::string_view` is not necessarily null-terminated, so passing
// its `data()` as a `const char*` attribute value would read out of bounds.
opentelemetry::nostd::string_view toOtel(std::string_view view) {
  return {view.data(), view.size()};
}

// Selects the exporter. Not read by the SDK itself (unlike the `OTEL_*`
// variables that configure the exporters and the batching), so we have to
// interpret it ourselves. The values follow the OTEL specification.
constexpr const char* TRACES_EXPORTER_ENV_VAR = "OTEL_TRACES_EXPORTER";

// Create the exporter selected by `OTEL_TRACES_EXPORTER`. Returns `nullptr`
// when spans should be dropped. The endpoint, headers and timeouts of the OTLP
// exporter come from `OTEL_EXPORTER_OTLP_*`, which its default options read.
std::unique_ptr<trace_sdk::SpanExporter> makeExporterFromEnv() {
  std::string exporter;
  if (!otel_common::GetStringEnvironmentVariable(TRACES_EXPORTER_ENV_VAR,
                                                 exporter) ||
      exporter.empty()) {
    exporter = "otlp";
  }
  if (exporter == "otlp") {
    return opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create();
  }
  if (exporter == "console") {
    return opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create();
  }
  if (exporter == "none") {
    return nullptr;
  }
  AD_LOG_WARN << "Unknown value \"" << exporter << "\" for "
              << TRACES_EXPORTER_ENV_VAR
              << ", falling back to \"otlp\". Supported values are \"otlp\", "
                 "\"console\" and \"none\"."
              << std::endl;
  return opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create();
}

}  // namespace

// _____________________________________________________________________________
TracingHandle::TracingHandle(
    std::shared_ptr<trace_sdk::TracerProvider> provider)
    : provider_{std::move(provider)} {}

// _____________________________________________________________________________
TracingHandle::TracingHandle(TracingHandle&& other) noexcept
    : provider_{std::move(other.provider_)} {}

// _____________________________________________________________________________
TracingHandle& TracingHandle::operator=(TracingHandle&& other) noexcept {
  if (this != &other) {
    shutdown();
    provider_ = std::move(other.provider_);
  }
  return *this;
}

// _____________________________________________________________________________
TracingHandle::~TracingHandle() { shutdown(); }

// _____________________________________________________________________________
void TracingHandle::shutdown() {
  if (provider_ == nullptr) {
    return;
  }
  // Uninstall first, so that anything that creates a span from here on gets the
  // no-op provider instead of one whose exporter is being torn down.
  trace_api::Provider::SetTracerProvider(
      opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>{});
  // The batch processor buffers spans and exports them from a background
  // thread, so without this the spans of the last few seconds would be lost.
  provider_->Shutdown();
  provider_ = nullptr;
}

// _____________________________________________________________________________
TracingHandle initialize(bool enabled) {
  if (!enabled) {
    return {};
  }
  auto exporter = makeExporterFromEnv();
  if (exporter == nullptr) {
    return {};
  }
  auto provider = trace_sdk::TracerProviderFactory::Create(
      trace_sdk::BatchSpanProcessorFactory::Create(
          std::move(exporter), trace_sdk::BatchSpanProcessorOptions{}),
      metrics::sharedResource(),
      // `ParentBased` rather than plain `AlwaysOn`, so that the sampling
      // decision of a client that sent a `traceparent` is honoured. Note that
      // there is deliberately no way to configure a ratio sampler here: it
      // would drop slow and failing requests at the same rate as fast ones,
      // which is backwards for a query engine. Reducing the volume is the job
      // of the collector's tail sampling, which sees the finished trace and can
      // therefore keep all errors and everything above a latency threshold.
      trace_sdk::ParentBasedSamplerFactory::Create(
          trace_sdk::AlwaysOnSamplerFactory::Create()));

  auto sharedProvider =
      std::shared_ptr<trace_sdk::TracerProvider>{std::move(provider)};
  trace_api::Provider::SetTracerProvider(
      opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>{
          std::shared_ptr<trace_api::TracerProvider>{sharedProvider}});

  // Lets `extractParentFromRequest` read a `traceparent` header, so that a
  // trace started by a client continues into QLever instead of being split into
  // two unrelated traces.
  opentelemetry::context::propagation::GlobalTextMapPropagator::
      SetGlobalPropagator(
          opentelemetry::nostd::shared_ptr<
              opentelemetry::context::propagation::TextMapPropagator>{
              new trace_api::propagation::HttpTraceContext{}});

  return TracingHandle{std::move(sharedProvider)};
}

// _____________________________________________________________________________
opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer() {
  return trace_api::Provider::GetTracerProvider()->GetTracer(TRACER_NAME);
}

// _____________________________________________________________________________
const trace_api::SpanContext& noParent() {
  static const trace_api::SpanContext invalid =
      trace_api::SpanContext::GetInvalid();
  return invalid;
}

// _____________________________________________________________________________
SpanGuard::SpanGuard(std::string_view name,
                     const trace_api::SpanContext& parent) {
  trace_api::StartSpanOptions options;
  if (parent.IsValid()) {
    options.parent = parent;
  } else {
    // An invalid parent is *not* enough to get a root span: the SDK then falls
    // back to whatever context happens to be attached to the current thread,
    // which in a coroutine is not something we control. Asking for a root span
    // explicitly is the documented way (see `StartSpanOptions::parent`).
    options.parent = opentelemetry::context::Context{}.SetValue(
        trace_api::kIsRootSpanKey, true);
  }
  span_ = tracer()->StartSpan(toOtel(name), options);
}

// _____________________________________________________________________________
SpanGuard::~SpanGuard() {
  if (!statusRecorded_) {
    // Neither success nor a specific error was recorded, so the scope holding
    // this guard was left abnormally. For a coroutine that usually means it was
    // cancelled or timed out and its frame was destroyed while suspended.
    span_->SetStatus(trace_api::StatusCode::kError, "unfinished");
  }
  span_->End();
}

// _____________________________________________________________________________
trace_api::SpanContext SpanGuard::context() const {
  return span_->GetContext();
}

// _____________________________________________________________________________
void SpanGuard::setOk() {
  span_->SetStatus(trace_api::StatusCode::kOk);
  statusRecorded_ = true;
}

// _____________________________________________________________________________
void SpanGuard::setError(std::string_view errorType, std::string_view message) {
  span_->SetAttribute(semconv::error::kErrorType, toOtel(errorType));
  span_->SetStatus(trace_api::StatusCode::kError, toOtel(message));
  statusRecorded_ = true;
}

// _____________________________________________________________________________
void SpanGuard::recordException(const std::exception& exception,
                                std::string_view errorType) {
  // The conventional representation of an exception on a span is an event named
  // `exception` carrying these two attributes.
  span_->AddEvent("exception",
                  {{semconv::exception::kExceptionType, toOtel(errorType)},
                   {semconv::exception::kExceptionMessage, exception.what()}});
  setError(errorType, exception.what());
}

}  // namespace ad_utility::tracing
