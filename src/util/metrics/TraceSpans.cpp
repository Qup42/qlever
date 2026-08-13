// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#include "util/metrics/TraceSpans.h"

#include <optional>
#include <string_view>
#include <utility>

#include "util/metrics/Tracing.h"

namespace trace_api = opentelemetry::trace;

namespace ad_utility::tracing {
namespace {

// Implements `timer::TraceSpan` in terms of `SpanGuard`, which already owns a
// span, ends it on destruction and records an unfinished status when none was
// set. A single class covers both roles: the adopted root span, which only
// supplies the context that children are parented under, and the spans that the
// tracer's phases create for themselves, which are owned and ended here.
class OtelTraceSpan : public timer::TraceSpan {
  // Empty for the adopted root span, whose span belongs to the caller.
  std::optional<SpanGuard> ownedSpan_;
  // The context that child spans are parented under.
  trace_api::SpanContext context_;

 public:
  // Adopt an already-running span, identified by its context.
  explicit OtelTraceSpan(trace_api::SpanContext adopted)
      : context_{std::move(adopted)} {}

  // Start a new span named `name` below `parent`.
  OtelTraceSpan(std::string_view name, trace_api::SpanContext parent)
      : ownedSpan_{std::in_place, name, std::move(parent)},
        context_{ownedSpan_->context()} {}

  void setOk() override {
    // Nothing to do for the adopted root span: the caller sets the status on
    // its own `SpanGuard`.
    if (ownedSpan_.has_value()) {
      ownedSpan_->setOk();
    }
  }

  std::unique_ptr<timer::TraceSpan> createChild(
      std::string_view name) override {
    return std::make_unique<OtelTraceSpan>(name, context_);
  }
};

}  // namespace

// _____________________________________________________________________________
std::unique_ptr<timer::TraceSpan> adoptSpanForTracer(
    trace_api::SpanContext parent) {
  return std::make_unique<OtelTraceSpan>(std::move(parent));
}

}  // namespace ad_utility::tracing
