
#ifndef LITECFI_CALL_GRAPH_H_
#define LITECFI_CALL_GRAPH_H_

#include <functional>
#include <set>
#include <string>

#include "CodeObject.h"
#include "parse.h"

template <typename T>
class LazyCallGraph;

// Implements a lazily evaluated call graph function. We lazily evaluate callees
// of the corresponding function and its associated data when visited during a
// call graph walk.
template <typename T>
struct LazyFunction {
 public:
  // Function name
  std::string name;
  // Underlying function definition
  Dyninst::ParseAPI::Function* function;

  // Fetches callees of this function
  std::set<LazyFunction<T>*> GetCallees();

  // Data associated with the function
  T* data = nullptr;

  LazyFunction(const LazyFunction&) = delete;
  LazyFunction& operator=(const LazyFunction&) = delete;

 private:
  // Makes construction private so that only LazyCallGraph can construct
  // LazyFunction instances
  LazyFunction() {}

  ~LazyFunction() { delete data; }

  // Gets the lazy function instance corresponding to the underlying function
  // definition. The parameter should correspond to an actual function
  // definition and not a plt stub function.
  static LazyFunction<T>* GetInstance(
      Dyninst::ParseAPI::Function* const function);

  std::set<LazyFunction<T>*> callees;
  // Keys are actual function definitions not plt stubs associated with any of
  // the functions
  static std::map<Dyninst::ParseAPI::Function*, LazyFunction<T>*> lazy_func_map;
  bool initialized = false;

  friend LazyCallGraph<T>;
};

// Implements a lazily evaluated call graph. Each call graph walk is initiated
// starting from some root function. Each such walk will evaluate the portion of
// the call graph induced by the root function.
template <typename T>
class LazyCallGraph {
 public:
  // Initializes a lazily evaluated call graph using provided parser information
  static LazyCallGraph<T>* GetCallGraph(const Parser& parser);

  // Gets a reference to the lazy function within the call graph
  LazyFunction<T>* GetFunction(std::string name);

  // Gets the underlying function definition
  static Dyninst::ParseAPI::Function* GetFunctionDefinition(std::string name);

  // Does a depth first visit of the call graph starting from root
  void VisitCallGraph(std::string root,
                      std::function<void(LazyFunction<T>* const)> callback);

 private:
  // Returns true if the function corresponds to a plt stub as opposed to an
  // actual function definition
  static bool IsPltFunction(
      Dyninst::ParseAPI::Function* function,
      const std::map<Dyninst::Address, std::string>& linkage);

  // Populates underlying function definitions. For a given function the
  // definition is the actual definition of the function not any of the plt
  // stubs corresponding to it.
  static void PopulateFunctionDefinitions(
      const std::vector<Dyninst::ParseAPI::CodeObject*>& objects);

  // Does a depth first visit at given node
  void VisitCallGraph(LazyFunction<T>* const function,
                      std::function<void(LazyFunction<T>* const)> callback,
                      std::set<LazyFunction<T>*>* const visited);

  static std::map<std::string, LazyFunction<T>*> functions_;
  // Holds actual function definitions not any of plt stubs corresponding to it
  static std::map<std::string, Dyninst::ParseAPI::Function*> function_defs_;
};

#include "call_graph_impl.h"

#endif  // LITECFI_CALL_GRAPH_H_
