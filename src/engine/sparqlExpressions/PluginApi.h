// Copyright 2025, University of Freiburg,
// Chair of Algorithms and Data Structures.

#ifndef QLEVER_SRC_ENGINE_SPARQLEXPRESSIONS_PLUGINAPI_H
#define QLEVER_SRC_ENGINE_SPARQLEXPRESSIONS_PLUGINAPI_H

#include "engine/sparqlExpressions/SparqlExpression.h"

// Increment this when the plugin ABI changes in an incompatible way.
#define QLEVER_PLUGIN_API_VERSION 1

// A single function registration entry provided by a plugin.
struct QleverFunctionRegistration {
  // Full IRI of the function (without angle brackets),
  // e.g. "http://example.org/functions#myFunc".
  const char* fullIri;

  // Number of arguments the function takes (0 = nullary, 1 = unary, etc.).
  int arity;

  // Factory function. Receives an array of `numArgs` expression pointers
  // (as `SparqlExpression::Ptr*`, i.e. `std::unique_ptr<SparqlExpression>*`).
  // The plugin may move out of each element. Returns an owning raw pointer
  // that the engine wraps in a `unique_ptr`. Using a raw pointer across the
  // shared-library boundary avoids ABI issues with `std::unique_ptr`.
  sparqlExpression::SparqlExpression* (*factory)(
      sparqlExpression::SparqlExpression::Ptr* args, int numArgs);
};

// Plugins must define the following functions with C linkage:
//
//   // Return the API version the plugin was compiled against.
//   extern "C" int qlever_plugin_api_version();
//
//   // Return a pointer to a static array of registrations and set `*count`
//   // to the number of entries. The engine reads exactly `*count` entries.
//   extern "C" const QleverFunctionRegistration*
//       qlever_plugin_register(int* count);
//
// Optionally, plugins may also define:
//
//   // Called before the shared library is unloaded. Use for cleanup.
//   extern "C" void qlever_plugin_cleanup();

#endif  // QLEVER_SRC_ENGINE_SPARQLEXPRESSIONS_PLUGINAPI_H
