// Copyright 2025, University of Freiburg,
// Chair of Algorithms and Data Structures.
//
// Example plugin demonstrating the custom function API.
//
// This plugin registers two custom SPARQL functions:
//
//   <http://example.org/functions#fortyTwo>()
//     A nullary function that always returns the integer 42.
//
//   <http://example.org/functions#double>(?x)
//     A unary function that doubles its numeric argument.
//
//   <http://wikiba.se/ontology#isSomeValue>(?x)
//     Equivalent to STRSTARTS(STR(?x),
//     "http://www.wikidata.org/.well-known/genid/"). Returns true if the value
//     is a Wikidata "unknown value" blank node.
//
// Usage:
//   qlever-server ... --load-plugin ./example_plugin.so
//
//   SELECT (<http://example.org/functions#fortyTwo>() AS ?x) WHERE { }
//   SELECT (<http://example.org/functions#double>(42) AS ?x) WHERE { }
//
// Build (from the qlever build directory):
//   cmake -DBUILD_EXAMPLE_PLUGINS=ON ..
//   make example_plugin

#include "engine/sparqlExpressions/NaryExpression.h"
#include "engine/sparqlExpressions/NaryExpressionImpl.h"
#include "engine/sparqlExpressions/PluginApi.h"

using namespace sparqlExpression;
using namespace sparqlExpression::detail;

// --- FortyTwo: nullary function returning 42 ---

// We use a simple IdExpression that always evaluates to the integer 42.
#include "engine/sparqlExpressions/LiteralExpression.h"

static SparqlExpression* makeFortyTwo(SparqlExpression::Ptr*, int) {
  return new IdExpression(Id::makeFromInt(42));
}

// --- Double: unary function that doubles a numeric value ---

// The implementation follows the same pattern as built-in numeric functions
// (see NumericUnaryExpressions.cpp).
struct DoubleValueImpl {
  template <typename T>
  auto operator()(T num) const {
    return num * 2;
  }
};

using DoubleValue = MakeNumericExpression<DoubleValueImpl>;
using DoubleExpression = NARY<1, FV<DoubleValue, NumericValueGetter>>;

static SparqlExpression* makeDouble(SparqlExpression::Ptr* args, int) {
  return new DoubleExpression(std::move(args[0]));
}

// --- isSomeValue: STRSTARTS(STR(?x),
// "http://www.wikidata.org/.well-known/genid/") ---

static SparqlExpression* makeIsSomeValue(SparqlExpression::Ptr* args, int) {
  // STR(?x)
  auto strExpr = makeStrExpression(std::move(args[0]));

  // Literal for the prefix string.
  using Literal = ad_utility::triple_component::Literal;
  auto prefixLit =
      Literal::literalWithNormalizedContent(asNormalizedStringViewUnsafe(
          "http://www.wikidata.org/.well-known/genid/"));
  auto prefixExpr =
      std::make_unique<StringLiteralExpression>(std::move(prefixLit));

  // STRSTARTS(STR(?x), prefix)
  return makeStrStartsExpression(std::move(strExpr), std::move(prefixExpr))
      .release();
}

// --- Plugin registration ---

static QleverFunctionRegistration registrations[] = {
    {"http://example.org/functions#fortyTwo", 0, &makeFortyTwo},
    {"http://example.org/functions#double", 1, &makeDouble},
    {"http://wikiba.se/ontology#isSomeValue", 1, &makeIsSomeValue},
};

extern "C" {
int qlever_plugin_api_version() { return QLEVER_PLUGIN_API_VERSION; }

const QleverFunctionRegistration* qlever_plugin_register(int* count) {
  *count = sizeof(registrations) / sizeof(registrations[0]);
  return registrations;
}
}
