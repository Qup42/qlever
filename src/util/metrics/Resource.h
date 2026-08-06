// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#ifndef QLEVER_SRC_UTIL_METRICS_RESOURCE_H
#define QLEVER_SRC_UTIL_METRICS_RESOURCE_H

#include <opentelemetry/sdk/resource/resource.h>

namespace ad_utility::metrics {

// The identity of this process, shared by all OTEL providers (currently the
// meter provider, later also the tracer provider) so that a backend can tell
// that metrics and spans come from the same QLever instance. The attributes
// describe the process and therefore belong here instead of being repeated on
// every metric and span.
//
// Initialized on first use. Call only after `qlever::version::copyVersionInfo`
// (see `ServerMain.cpp`), because the build information is read from the
// variables that function populates.
const opentelemetry::sdk::resource::Resource& sharedResource();

// Whether the environment specifies a `service.name`, either directly via
// `OTEL_SERVICE_NAME` or as an entry of `OTEL_RESOURCE_ATTRIBUTES`. Exposed for
// testing; `sharedResource` uses it to decide whether to supply a default, see
// the comment on `kDefaultServiceName` in the implementation.
bool hasServiceNameFromEnv();

}  // namespace ad_utility::metrics

#endif  // QLEVER_SRC_UTIL_METRICS_RESOURCE_H
