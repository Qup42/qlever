// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#ifndef QLEVER_SRC_UTIL_METRICS_TRACESPANS_H
#define QLEVER_SRC_UTIL_METRICS_TRACESPANS_H

#include <opentelemetry/trace/span_context.h>

#include <memory>

#include "util/TimeTracer.h"

namespace ad_utility::tracing {

// The OpenTelemetry implementation of `timer::TraceSpan`. Pass the result to a
// `TimeTracer` to make every phase it records an OTEL span, nested exactly like
// the phases are.
//
// The returned object stands for the tracer's root trace and adopts the
// already-running span `parent`, so that the phases become spans below it. The
// adopted span is not owned: it is neither ended nor given a status here,
// because the `SpanGuard` that created it does that itself.
std::unique_ptr<timer::TraceSpan> adoptSpanForTracer(
    opentelemetry::trace::SpanContext parent);

}  // namespace ad_utility::tracing

#endif  // QLEVER_SRC_UTIL_METRICS_TRACESPANS_H
