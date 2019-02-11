
#include "call_graph.h"

#include <algorithm>
#include <string>

#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "CodeObject.h"
#include "glog/logging.h"
#include "parse.h"

// ------------------ LazyFunction ------------------------ //

std::map<Dyninst::ParseAPI::Function*, LazyFunction*>
    LazyFunction::lazy_func_map;

LazyFunction* LazyFunction::GetNewInstance(
    Dyninst::ParseAPI::Function* function) {
  auto it = lazy_func_map.find(function);
  if (it != lazy_func_map.end()) {
    return it->second;
  }

  LazyFunction* lazy_function = new LazyFunction();
  lazy_func_map.insert(std::pair<Dyninst::ParseAPI::Function*, LazyFunction*>(
      function, lazy_function));
  return lazy_function;
}

std::set<LazyFunction*> LazyFunction::GetCallees() {
  if (initialized) {
    // Callees already initialized
    return callees;
  }

  // Find the call edges from this function
  const Dyninst::ParseAPI::Function::edgelist& call_edges =
      function->callEdges();

  // Iterate and find call edge targets
  for (auto eit = call_edges.begin(); eit != call_edges.end(); eit++) {
    Dyninst::ParseAPI::Edge* edge = *eit;

    // Only consider inter procedural edges
    if (edge->interproc()) {
      Dyninst::ParseAPI::Block* block = edge->trg();

      std::vector<Dyninst::ParseAPI::Function*> functions;
      // Get the target functions of this call edge
      block->getFuncs(functions);

      for (auto func : functions) {
        LazyFunction* lazy_function;

        // There should be a LazyFunction entry for this function since call
        // graph processing should have happened by this stage
        auto map_iter = lazy_func_map.find(func);
        DCHECK(map_iter != lazy_func_map.end());

        lazy_function = map_iter->second;
        callees.insert(lazy_function);
      }
    }
  }

  initialized = true;
  return callees;
}

// -------------------- LazyCallGraph --------------------- //

std::map<std::string, LazyFunction*> LazyCallGraph::functions_;

LazyCallGraph* LazyCallGraph::GetCallGraph(const Parser& parser) {
  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  LazyCallGraph::functions_.clear();
  LazyCallGraph* cg = new LazyCallGraph();

  for (auto object : objects) {
    Dyninst::ParseAPI::CodeObject* code_object =
        Dyninst::ParseAPI::convert(object);

    for (auto function : code_object->funcs()) {
      LazyFunction* lazy_function = LazyFunction::GetNewInstance(function);
      lazy_function->name = function->name();
      lazy_function->code_object = object->pathName();
      lazy_function->function = function;

      cg->functions_.insert(std::pair<std::string, LazyFunction*>(
          function->name(), lazy_function));
    }
  }
  return cg;
}

LazyFunction* LazyCallGraph::GetFunction(std::string name) {
  auto it = functions_.find(name);
  DCHECK(it != functions_.end());
  return it->second;
}

void LazyCallGraph::VisitCallGraph(
    LazyFunction* function, std::function<void(LazyFunction*)> callback) {
  // Run callback on current node
  callback(function);

  // Visit children
  std::set<LazyFunction*> callees = function->GetCallees();
  for (LazyFunction* callee : callees) {
    VisitCallGraph(callee, callback);
  }
}

void LazyCallGraph::VisitCallGraph(
    std::string root, std::function<void(LazyFunction*)> callback) {
  VisitCallGraph(GetFunction(root), callback);
}
