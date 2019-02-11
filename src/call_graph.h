
#ifndef LITECFI_CALL_GRAPH_H_
#define LITECFI_CALL_GRAPH_H_

#include <functional>
#include <set>
#include <string>

#include "CodeObject.h"
#include "parse.h"

class LazyCallGraph;

struct LazyFunction {
 public:
  std::string name;
  std::string code_object;
  Dyninst::ParseAPI::Function* function;

  std::set<LazyFunction*> GetCallees();

  LazyFunction(const LazyFunction&) = delete;
  LazyFunction& operator=(const LazyFunction&) = delete;

 private:
  // Make construction private so that only LazyCallGraph can construct
  // LazyFunction instances
  LazyFunction() {}

  static LazyFunction* GetNewInstance(Dyninst::ParseAPI::Function* function);

  std::set<LazyFunction*> callees;
  static std::map<Dyninst::ParseAPI::Function*, LazyFunction*> lazy_func_map;
  bool initialized = false;

  friend LazyCallGraph;
};

class LazyCallGraph {
 public:
  // Initialize a callgraph using provided parser information
  static LazyCallGraph* GetCallGraph(const Parser& parser);

  // Gets a reference to the function within the call graph
  LazyFunction* GetFunction(std::string);

  // Do a depth first visit of the call graph starting from root
  void VisitCallGraph(std::string root,
                      std::function<void(LazyFunction*)> callback);

 private:
  // Do a depth first visit at given node
  void VisitCallGraph(LazyFunction* function,
                      std::function<void(LazyFunction*)> callback);

  static std::map<std::string, LazyFunction*> functions_;
};

#endif  // LITECFI_CALL_GRAPH_H_
