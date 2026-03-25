// Copyright 2025, University of Freiburg,
// Chair of Algorithms and Data Structures.

#include "engine/sparqlExpressions/CustomFunctionRegistry.h"

#include <dlfcn.h>

#include "engine/sparqlExpressions/PluginApi.h"
#include "util/Log.h"

namespace sparqlExpression {

// ____________________________________________________________________________
CustomFunctionRegistry& CustomFunctionRegistry::instance() {
  static CustomFunctionRegistry registry;
  return registry;
}

// ____________________________________________________________________________
void CustomFunctionRegistry::loadPlugins(
    const std::vector<std::string>& pluginPaths) {
  for (const auto& path : pluginPaths) {
    AD_LOG_INFO << "Loading plugin: " << path << std::endl;

    // Open the shared library.
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      throw std::runtime_error(
          absl::StrCat("Failed to load plugin \"", path, "\": ", dlerror()));
    }

    // Clear any previous error.
    dlerror();

    // Check API version.
    auto versionFunc =
        reinterpret_cast<int (*)()>(dlsym(handle, "qlever_plugin_api_version"));
    if (!versionFunc) {
      dlclose(handle);
      throw std::runtime_error(absl::StrCat(
          "Plugin \"", path, "\" does not export qlever_plugin_api_version"));
    }
    int version = versionFunc();
    if (version != QLEVER_PLUGIN_API_VERSION) {
      dlclose(handle);
      throw std::runtime_error(
          absl::StrCat("Plugin \"", path, "\" has API version ", version,
                       ", but expected ", QLEVER_PLUGIN_API_VERSION));
    }

    // Get the registration function.
    auto registerFunc =
        reinterpret_cast<const QleverFunctionRegistration* (*)(int*)>(
            dlsym(handle, "qlever_plugin_register"));
    if (!registerFunc) {
      dlclose(handle);
      throw std::runtime_error(absl::StrCat(
          "Plugin \"", path, "\" does not export qlever_plugin_register"));
    }

    // Get registrations.
    int count = 0;
    const QleverFunctionRegistration* regs = registerFunc(&count);
    if (!regs || count <= 0) {
      dlclose(handle);
      throw std::runtime_error(absl::StrCat(
          "Plugin \"", path, "\" returned no function registrations"));
    }

    // Register each function.
    for (int i = 0; i < count; ++i) {
      const auto& reg = regs[i];
      if (!reg.fullIri || !reg.factory) {
        dlclose(handle);
        throw std::runtime_error(absl::StrCat(
            "Plugin \"", path, "\" has invalid registration at index ", i));
      }

      std::string iri(reg.fullIri);
      int arity = reg.arity;
      auto rawFactory = reg.factory;

      if (registry_.contains(iri)) {
        AD_LOG_WARN << "Custom function <" << iri
                    << "> is being replaced by plugin: " << path << std::endl;
      }

      Entry entry;
      entry.fullIri = iri;
      entry.arity = arity;
      entry.factory = [rawFactory, arity,
                       iri](std::vector<SparqlExpression::Ptr> args)
          -> SparqlExpression::Ptr {
        AD_CONTRACT_CHECK(static_cast<int>(args.size()) == arity);
        SparqlExpression* raw = rawFactory(args.data(), arity);
        if (!raw) {
          throw std::runtime_error(absl::StrCat("Custom function <", iri,
                                                "> factory returned nullptr"));
        }
        return SparqlExpression::Ptr(raw);
      };

      registry_.insert_or_assign(std::move(iri), std::move(entry));
      AD_LOG_INFO << "  Registered custom function: <" << reg.fullIri
                  << "> (arity " << arity << ")" << std::endl;
    }

    // Optionally get the cleanup function.
    auto cleanupFunc =
        reinterpret_cast<void (*)()>(dlsym(handle, "qlever_plugin_cleanup"));

    loadedPlugins_.push_back(
        {.handle = handle, .path = path, .cleanup = cleanupFunc});
  }
}

// ____________________________________________________________________________
const CustomFunctionRegistry::Entry* CustomFunctionRegistry::lookup(
    std::string_view fullIri) const {
  auto it = registry_.find(std::string(fullIri));
  if (it != registry_.end()) {
    return &it->second;
  }
  return nullptr;
}

// ____________________________________________________________________________
bool CustomFunctionRegistry::empty() const { return registry_.empty(); }

// ____________________________________________________________________________
CustomFunctionRegistry::~CustomFunctionRegistry() {
  for (auto& plugin : loadedPlugins_) {
    if (plugin.cleanup) {
      plugin.cleanup();
    }
    if (plugin.handle) {
      dlclose(plugin.handle);
    }
  }
}

}  // namespace sparqlExpression
