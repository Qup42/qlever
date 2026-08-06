// Copyright 2026 The QLever Authors, in particular:
//
// 2026 Julian Mundhahs <mundhahj@tf.uni-freiburg.de>, UFR
//
// UFR = University of Freiburg, Chair of Algorithms and Data Structures

// You may not use this file except in compliance with the Apache 2.0 License,
// which can be found in the `LICENSE` file at the root of the QLever project.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <opentelemetry/semconv/service_attributes.h>

#include <cstdlib>
#include <string>

#include "util/metrics/Metrics.h"
#include "util/metrics/Resource.h"

namespace {
namespace semconv = opentelemetry::semconv;

// `sharedResource` caches its result on first use, and the default
// `service.name` it applies depends on the environment. Clear the relevant
// variables before any test runs, so that the expectations below hold no matter
// what the environment of the test runner looks like.
class ClearOtelResourceEnvironment : public testing::Environment {
 public:
  void SetUp() override {
    unsetenv("OTEL_SERVICE_NAME");
    unsetenv("OTEL_RESOURCE_ATTRIBUTES");
  }
};
const testing::Environment* const otelResourceEnvironment =
    testing::AddGlobalTestEnvironment(new ClearOtelResourceEnvironment{});

// Return the value of the given string attribute of the shared resource, or
// `std::nullopt` if the resource has no such attribute.
std::optional<std::string> resourceAttribute(std::string_view key) {
  const auto& attributes =
      ad_utility::metrics::sharedResource().GetAttributes();
  auto it = attributes.find(std::string{key});
  if (it == attributes.end()) {
    return std::nullopt;
  }
  return opentelemetry::nostd::get<std::string>(it->second);
}

// RAII helper that sets an environment variable and unsets it again on
// destruction.
class ScopedEnvironmentVariable {
  std::string name_;

 public:
  ScopedEnvironmentVariable(std::string name, const std::string& value)
      : name_{std::move(name)} {
    setenv(name_.c_str(), value.c_str(), /*overwrite=*/1);
  }
  ~ScopedEnvironmentVariable() { unsetenv(name_.c_str()); }
};
}  // namespace

// _____________________________________________________________________________
TEST(Metrics, initialize) {
  EXPECT_EQ(ad_utility::metrics::initialize(false), nullptr);
  EXPECT_NE(ad_utility::metrics::initialize(true), nullptr);
}

// _____________________________________________________________________________
TEST(Metrics, sharedResourceContainsBuildInformation) {
  // The values are only meaningful after `copyVersionInfo` has been called,
  // which the test binary does not do. Check that the attributes exist and are
  // non-empty; their contents are covered by the `qlever.build_info` metric.
  for (std::string_view key :
       {"qlever.git_hash", "qlever.compiler", "qlever.compiler_version",
        "qlever.cxx_standard", "qlever.compile_time",
        semconv::service::kServiceVersion}) {
    auto value = resourceAttribute(key);
    ASSERT_TRUE(value.has_value()) << "missing attribute " << key;
    EXPECT_THAT(value.value(), testing::Not(testing::IsEmpty()))
        << "empty attribute " << key;
  }
}

// _____________________________________________________________________________
TEST(Metrics, sharedResourceUsesDefaultServiceName) {
  // With no `service.name` in the environment (ensured by
  // `ClearOtelResourceEnvironment`) we supply one ourselves, because the SDK
  // would otherwise substitute the useless literal `unknown_service`.
  EXPECT_THAT(resourceAttribute(semconv::service::kServiceName),
              testing::Optional(std::string{"qlever"}));
}

// _____________________________________________________________________________
TEST(Metrics, hasServiceNameFromEnv) {
  // Note that this cannot be checked on `sharedResource()` itself: both that
  // function and `Resource::Create`'s environment detector cache their result,
  // so only the first of these cases would ever be observable there.
  EXPECT_FALSE(ad_utility::metrics::hasServiceNameFromEnv());
  {
    ScopedEnvironmentVariable variable{"OTEL_SERVICE_NAME", "my-qlever"};
    EXPECT_TRUE(ad_utility::metrics::hasServiceNameFromEnv());
  }
  {
    // An empty value does not count as a service name.
    ScopedEnvironmentVariable variable{"OTEL_SERVICE_NAME", ""};
    EXPECT_FALSE(ad_utility::metrics::hasServiceNameFromEnv());
  }
  {
    ScopedEnvironmentVariable variable{"OTEL_RESOURCE_ATTRIBUTES",
                                       "service.name=my-qlever"};
    EXPECT_TRUE(ad_utility::metrics::hasServiceNameFromEnv());
  }
  {
    // Not the first entry, and with whitespace around the key.
    ScopedEnvironmentVariable variable{
        "OTEL_RESOURCE_ATTRIBUTES",
        "deployment.environment=prod, service.name=my-qlever"};
    EXPECT_TRUE(ad_utility::metrics::hasServiceNameFromEnv());
  }
  {
    // A different attribute must not be mistaken for the service name, in
    // particular not one that has `service.name` as a prefix or suffix.
    ScopedEnvironmentVariable variable{
        "OTEL_RESOURCE_ATTRIBUTES",
        "service.namespace=qlever,my.service.name=x"};
    EXPECT_FALSE(ad_utility::metrics::hasServiceNameFromEnv());
  }
  EXPECT_FALSE(ad_utility::metrics::hasServiceNameFromEnv());
}
