// Copyright 2025 The QLever Authors, in particular:
//
// 2025 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

#include <memory>
#include <string_view>

#include "util/Timer.h"
#include "util/json.h"

#ifndef QLEVER_TIMETRACER_H
#define QLEVER_TIMETRACER_H

namespace ad_utility::timer {

// A span in an external tracing backend, attached to a `Trace` below. Ends the
// span when destroyed, so that a phase which is never ended explicitly (because
// an exception unwound past its `endTrace`) is still exported.
//
// This is an abstract interface rather than the OpenTelemetry span type,
// because this header is part of `qlever_util`, on which the `metrics` library
// depends. Using OTEL here directly would be a cyclic dependency, and would
// additionally pull OTEL into `index` and `engine` via `DeltaTriples.h` and
// `ExecuteUpdate.h`. The implementation is in `util/metrics/TraceSpans.h`.
class TraceSpan {
 public:
  virtual ~TraceSpan() = default;

  // Record that the phase finished successfully. Called by `endTrace`.
  virtual void setOk() = 0;

  // Start a span for a phase nested inside this one.
  virtual std::unique_ptr<TraceSpan> createChild(std::string_view name) = 0;
};

struct Trace {
  std::string name_;
  // The times are recorded with microsecond precision, but reported (in the
  // JSON output below) rounded to milliseconds.
  std::chrono::microseconds begin_;
  std::optional<std::chrono::microseconds> end_ = std::nullopt;
  std::vector<Trace> children_ = {};
  // The span for this phase in an external tracing backend, or `nullptr` when
  // there is no backend. Not part of the JSON representation below.
  std::unique_ptr<TraceSpan> span_ = nullptr;

  std::chrono::microseconds duration() const {
    if (!end_) {
      throw std::runtime_error("Trace has not yet ended.");
    }
    return end_.value() - begin_;
  }

  // Round a recorded time to milliseconds for the JSON output.
  static int64_t toMillis(std::chrono::microseconds time) {
    return std::chrono::round<std::chrono::milliseconds>(time).count();
  }

  // Output a finished tracer as json. The signature of this function is
  // mandated by the json library to allow for implicit conversion.
  friend void to_json(nlohmann::ordered_json& j, const Trace& trace) {
    j = nlohmann::ordered_json{{"name", trace.name_},
                               {"begin", toMillis(trace.begin_)},
                               {"end", toMillis(trace.end_.value())},
                               {"duration", toMillis(trace.duration())}};
    if (!trace.children_.empty()) {
      nlohmann::ordered_json children = nlohmann::ordered_json::array();
      for (const Trace& childTrace : trace.children_) {
        children.push_back(childTrace);
      }
      j["children"] = children;
    }
  }

  // Return a short json representation of a finished tracer.
  friend void to_json_short(nlohmann::ordered_json& j, const Trace& trace) {
    if (trace.children_.empty()) {
      j[trace.name_] = toMillis(trace.duration());
    } else {
      nlohmann::ordered_json childJ = {{"total", toMillis(trace.duration())}};
      for (const Trace& childTrace : trace.children_) {
        to_json_short(childJ, childTrace);
      }
      j[trace.name_] = childJ;
    }
  }
};

class TimeTracer {
  Timer timer_ = Timer(Timer::Started);
  Trace rootTrace_;
  // The chain of currently running traces, outermost first. These are
  // references into the `children_` vectors of `rootTrace_`. That is sound
  // because traces are strictly nested: a sibling is only appended to a
  // `children_` vector after the previous sibling has been ended and popped, so
  // the reallocation in `push_back` can never invalidate a reference that is
  // still in here.
  std::vector<std::reference_wrapper<Trace>> activeTraces_;

 public:
  // `rootSpan` is the span that the root trace corresponds to. Pass `nullptr`
  // (the default) to only record the timings. Note that the root span is
  // adopted, not owned: it is neither ended nor given a status by this class,
  // because the caller that created it does that itself.
  explicit TimeTracer(const std::string& name,
                      std::unique_ptr<TraceSpan> rootSpan = nullptr)
      : rootTrace_{name, std::chrono::microseconds::zero()},
        activeTraces_({rootTrace_}) {
    rootTrace_.span_ = std::move(rootSpan);
  }
  virtual ~TimeTracer() = default;

  // Not copyable or movable: `activeTraces_` holds references into
  // `rootTrace_`, which a copy or a move would leave pointing into the original
  // object.
  TimeTracer(const TimeTracer&) = delete;
  TimeTracer& operator=(const TimeTracer&) = delete;
  TimeTracer(TimeTracer&&) = delete;
  TimeTracer& operator=(TimeTracer&&) = delete;

  virtual void beginTrace(const std::string& name) {
    if (activeTraces_.empty()) {
      throw std::runtime_error("The trace has ended.");
    }
    Trace& parent = activeTraces_.back().get();
    parent.children_.push_back({name, timer_.value()});
    Trace& child = parent.children_.back();
    if (parent.span_ != nullptr) {
      child.span_ = parent.span_->createChild(name);
    }
    activeTraces_.emplace_back(child);
  }

  virtual void endTrace(std::string_view name) {
    if (activeTraces_.empty()) {
      throw std::runtime_error("The trace has ended.");
    }

    Trace& activeTrace = activeTraces_.back().get();
    if (activeTrace.name_ != name) {
      throw std::runtime_error(
          absl::StrCat("Tried to end trace \"", name, "\", but trace \"",
                       activeTrace.name_, "\" was running."));
    }
    activeTrace.end_ = timer_.value();
    if (activeTrace.span_ != nullptr) {
      activeTrace.span_->setOk();
      // Destroying the span is what ends it, so that its duration is that of
      // the phase and not that of this tracer.
      activeTrace.span_.reset();
    }
    activeTraces_.pop_back();
  }

  // Resets the tracer to its initial state and restarts the root trace.
  virtual void reset() {
    if (!activeTraces_.empty()) {
      throw std::runtime_error(
          "Cannot reset a TimeTracer that has active traces.");
    }

    rootTrace_.begin_ = timer_.value();
    rootTrace_.end_ = std::nullopt;
    rootTrace_.children_.clear();
    activeTraces_.emplace_back(rootTrace_);
  }

  virtual nlohmann::ordered_json getJSON() const { return rootTrace_; }
  virtual nlohmann::ordered_json getJSONShort() const {
    nlohmann::ordered_json j;
    to_json_short(j, rootTrace_);
    return j;
  }
};

// A time tracer that does nothing, which will be used a default argument in
// all methods that take a TimeTracer. This is useful for testing, so that we
// don't have to pass a TimeTracer to every method that uses one.
class DefaultTimeTracer : public TimeTracer {
 public:
  using TimeTracer::TimeTracer;
  void beginTrace(const std::string&) override {
    // `DefaultTimeTracer` does nothing.
  }
  void endTrace(std::string_view) override {
    // `DefaultTimeTracer` does nothing.
  }
  nlohmann::ordered_json getJSON() const override { return {}; }
  nlohmann::ordered_json getJSONShort() const override { return {}; }
};

static inline DefaultTimeTracer DEFAULT_TIME_TRACER = DefaultTimeTracer("");

}  // namespace ad_utility::timer

#endif  // QLEVER_TIMETRACER_H
