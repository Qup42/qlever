// Copyright 2025 The QLever Authors, in particular:
//
// 2025 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

#include "util/Timer.h"
#include "util/json.h"

#ifndef QLEVER_TIMETRACER_H
#define QLEVER_TIMETRACER_H

namespace ad_utility::timer {

struct Trace {
  std::string name_;
  // The times are recorded with microsecond precision, but reported (in the
  // JSON output below) rounded to milliseconds.
  std::chrono::microseconds begin_;
  std::optional<std::chrono::microseconds> end_ = std::nullopt;
  std::vector<Trace> children_ = {};

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
  std::vector<std::reference_wrapper<Trace>> activeTraces_;

 public:
  explicit TimeTracer(const std::string& name)
      : rootTrace_{name, std::chrono::microseconds::zero()},
        activeTraces_({rootTrace_}) {}
  virtual ~TimeTracer() = default;

  virtual void beginTrace(const std::string& name) {
    if (activeTraces_.empty()) {
      throw std::runtime_error("The trace has ended.");
    }
    activeTraces_.back().get().children_.push_back({name, timer_.value()});
    activeTraces_.emplace_back(activeTraces_.back().get().children_.back());
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
