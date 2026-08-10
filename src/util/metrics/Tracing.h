// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#ifndef QLEVER_SRC_UTIL_METRICS_TRACING_H
#define QLEVER_SRC_UTIL_METRICS_TRACING_H

#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/context/propagation/text_map_propagator.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/version.h>

#include <exception>
#include <memory>
#include <optional>
#include <string_view>

#include "util/Exception.h"
#include "util/http/beast.h"

// Forward declaration, so that this header does not have to pull in the OTEL
// trace SDK. OTEL uses custom macros for its namespaces.
OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk::trace {
class TracerProvider;
}
OPENTELEMETRY_END_NAMESPACE

namespace ad_utility::tracing {

// Sets up tracing. On destruction tracing is reset to the no-op provider.
class [[nodiscard(
    "Tracing is only active while this handle is alive.")]] TracingHandle {
  // Empty when tracing is disabled. The SDK type rather than the API type,
  // because only the former can be shut down.
  std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> provider_;

 public:
  TracingHandle() = default;
  explicit TracingHandle(
      std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> provider);
  ~TracingHandle();

  TracingHandle(TracingHandle&&) noexcept;
  TracingHandle& operator=(TracingHandle&&) noexcept;
  TracingHandle(const TracingHandle&) = delete;
  TracingHandle& operator=(const TracingHandle&) = delete;

  void shutdown();
};

// Install the global OTEL `TracerProvider`, and the W3C propagator used to read
// a `traceparent` header from incoming requests. When `enabled` is false this
// does nothing at all: the provider remains the no-op provider that the API
// installs by default, which makes every `StartSpan` below a cheap no-op. Call
// sites therefore need no `if (tracingEnabled)` guards.
//
// Which exporter is used, and where it sends spans, is taken from the standard
// OTEL environment variables:
//   OTEL_EXPORTER_OTLP_TRACES_ENDPOINT, OTEL_EXPORTER_OTLP_ENDPOINT
//   OTEL_BSP_*                        batching behaviour
//   OTEL_SERVICE_NAME, OTEL_RESOURCE_ATTRIBUTES   see `Resource.h`
//
// Must be called once at startup, before anything creates a span.
[[nodiscard]] TracingHandle initialize(bool enabled);

// The single tracer of this process. Cheap to call: the provider looks up an
// existing tracer by name.
opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer();

// Owns a span and ends it on destruction.
//
// Ending from the destructor matters for more than convenience: a span that is
// only ended on the success path is never exported and its memory is held by
// the processor. Because QLever's request handling is built from coroutines
// that can be cancelled or time out, the only reliable place to end a span is
// the destructor of the coroutine frame, which is what this achieves.
//
// If neither `setOk` nor an error was recorded by the time the guard is
// destroyed, the span is marked as an error, on the assumption that the frame
// was destroyed abnormally.
class [[nodiscard(
    "The span is only ended when this guard is destroyed. Store it in a "
    "variable.")]] SpanGuard {
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span_;
  bool statusRecorded_ = false;

 public:
  // Start a span named `name` as a child of `parent`, or a span that starts a
  // new trace when `parent` is `std::nullopt`. Note that the latter is *not*
  // the same as leaving the parent unset in OTEL: an unset (or invalid) parent
  // makes the SDK fall back to whatever context happens to be attached to the
  // current thread, whereas `std::nullopt` here is an explicit request for a
  // root span. Takes `parent` by value, because it is copied into the span
  // either way.
  SpanGuard(std::string_view name,
            std::optional<opentelemetry::trace::SpanContext> parent);
  ~SpanGuard();

  SpanGuard(const SpanGuard&) = delete;
  SpanGuard& operator=(const SpanGuard&) = delete;

  opentelemetry::trace::Span& span() const { return *span_; }

  // The owned span as a shared pointer, which is what `Tracer::WithActiveSpan`
  // needs. Only required where the thread-local OTEL context has to be attached
  // for a synchronous stretch of code; prefer passing `context()` to a child.
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> sharedSpan()
      const {
    return span_;
  }

  // The context of this span, to be passed as the parent of child spans.
  // Explicit parenting is required in coroutines, because the thread-local
  // context is not reliable across a `co_await`.
  // Returns by value, because `Span::GetContext` does.
  opentelemetry::trace::SpanContext context() const;

  // Record that the work the span describes finished successfully.
  void setOk();

  // Record a failure. `errorType` should be one of the values also used for the
  // `type` label of the error metrics (see `Metrics.h`), so that spans and
  // metrics can be filtered the same way.
  void setError(std::string_view errorType, std::string_view message);

  // Like `setError`, and additionally records the exception's type and message
  // as an `exception` event on the span.
  void recordException(const std::exception& exception,
                       std::string_view errorType);
};

// Read-only `TextMapCarrier` over the headers of an incoming HTTP request, so
// that the propagator can extract a `traceparent` a client may have sent. The
// referenced request has to outlive the carrier.
//
// Only the extracting direction is implemented. Injecting into an outgoing
// request (to propagate into federated `SERVICE` requests) needs a writable
// carrier, and `HttpClient` cannot currently send arbitrary headers anyway.
template <typename RequestT>
class RequestHeaderCarrier
    : public opentelemetry::context::propagation::TextMapCarrier {
  const RequestT& request_;

 public:
  explicit RequestHeaderCarrier(const RequestT& request) : request_{request} {}

  opentelemetry::nostd::string_view Get(
      opentelemetry::nostd::string_view key) const noexcept override {
    auto it =
        request_.base().find(boost::beast::string_view{key.data(), key.size()});
    if (it == request_.base().end()) {
      return {};
    }
    return {it->value().data(), it->value().size()};
  }

  void Set(opentelemetry::nostd::string_view,
           opentelemetry::nostd::string_view) noexcept override {
    // Unreachable: this carrier is only ever used for extraction. Not throwing,
    // because the interface is `noexcept`.
    AD_FAIL();
  }
};

// Extract the span context a client sent via the `traceparent` header of
// `request`, using the propagator installed by `initialize`. Returns
// `std::nullopt` when there is no such header or it is malformed, which is what
// `SpanGuard` expects for a span that starts a new trace. In particular this
// never throws, so that a bad header cannot fail the request.
template <typename RequestT>
std::optional<opentelemetry::trace::SpanContext> extractParentFromRequest(
    const RequestT& request) {
  RequestHeaderCarrier<RequestT> carrier{request};
  // `Extract` takes its input context by non-const reference, so it needs a
  // named variable rather than a temporary.
  opentelemetry::context::Context emptyContext{};
  auto context = opentelemetry::context::propagation::GlobalTextMapPropagator::
                     GetGlobalPropagator()
                         ->Extract(carrier, emptyContext);
  auto parent = opentelemetry::trace::GetSpan(context)->GetContext();
  if (!parent.IsValid()) {
    return std::nullopt;
  }
  return parent;
}

}  // namespace ad_utility::tracing

#endif  // QLEVER_SRC_UTIL_METRICS_TRACING_H
