// Copyright 2025, University of Freiburg,
// Chair of Algorithms and Data Structures.

#ifndef QLEVER_SRC_ENGINE_SPARQLEXPRESSIONS_CUSTOMFUNCTIONREGISTRY_H
#define QLEVER_SRC_ENGINE_SPARQLEXPRESSIONS_CUSTOMFUNCTIONREGISTRY_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/sparqlExpressions/SparqlExpression.h"

namespace sparqlExpression {

// A singleton registry of custom SPARQL functions loaded from shared-library
// plugins at server startup. The registry is populated once before any query
// threads are started, so concurrent reads during query processing are safe
// without locks.
class CustomFunctionRegistry {
 public:
  // Factory signature: takes a vector of child expressions, returns a new
  // expression.
  using FactoryFunction =
      std::function<SparqlExpression::Ptr(std::vector<SparqlExpression::Ptr>)>;

  // A registered custom function.
  struct Entry {
    std::string fullIri;
    int arity;
    FactoryFunction factory;
  };

  // Get the global singleton instance.
  static CustomFunctionRegistry& instance();

  // Load plugins from the given shared-library paths. Throws on any failure
  // (missing file, wrong API version, missing symbols, etc.).
  void loadPlugins(const std::vector<std::string>& pluginPaths);

  // Look up a custom function by its full IRI. Returns nullptr if not found.
  const Entry* lookup(std::string_view fullIri) const;

  // Return true if the registry contains any custom functions.
  bool empty() const;

  ~CustomFunctionRegistry();

  // Not copyable or movable (singleton).
  CustomFunctionRegistry(const CustomFunctionRegistry&) = delete;
  CustomFunctionRegistry& operator=(const CustomFunctionRegistry&) = delete;

 private:
  CustomFunctionRegistry() = default;

  // Maps full IRI string -> Entry.
  std::unordered_map<std::string, Entry> registry_;

  // Keeps loaded shared libraries alive for the lifetime of the process.
  struct LoadedPlugin {
    void* handle = nullptr;
    std::string path;
    void (*cleanup)() = nullptr;
  };
  std::vector<LoadedPlugin> loadedPlugins_;
};

}  // namespace sparqlExpression

#endif  // QLEVER_SRC_ENGINE_SPARQLEXPRESSIONS_CUSTOMFUNCTIONREGISTRY_H
