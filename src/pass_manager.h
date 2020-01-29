#ifndef LITECFI_PASS_MANAGER_H_
#define LITECFI_PASS_MANAGER_H_

#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "CodeObject.h"
#include "DynAST.h"
#include "gflags/gflags.h"
#include "utils.h"

using Dyninst::Address;
using Dyninst::AST;
using Dyninst::InstructionAPI::Instruction;
using Dyninst::ParseAPI::Block;
using Dyninst::ParseAPI::CALL;
using Dyninst::ParseAPI::CALL_FT;
using Dyninst::ParseAPI::CodeObject;
using Dyninst::ParseAPI::Function;
using Dyninst::ParseAPI::RET;

DECLARE_bool(vv);
DECLARE_string(stats);

extern std::set<Address> exception_free_func;

struct MoveInstData {
  // Pre-instruction address for moving instrumentation
  Address newInstAddress;
  int raOffset;

  int saveCount;
  std::string reg1;
  std::string reg2;
};

struct MemoryWrite {
  MemoryWrite():
    function(nullptr),
    block(nullptr),
    addr(0),
    stack(false),
    resolved(false),
    global(false),
    heap(false),
    arg(false),
    heap_or_arg(false) {}

  // Memory write coordinate information.
  Instruction ins;
  Function* function;
  Block* block;

  Address addr;

  // Resolved symbolic expression(s) for the value of the memory write at the
  // function entry.
  std::vector<AST::Ptr> defines;
  // Resolved symbolic expression(s) for the value of the memory write at the
  // function exit.
  std::vector<AST::Ptr> uses;

  // Denotes if this is a stack write.
  bool stack;
  // Denotes if this write has been resolved for defines and uses.
  bool resolved;
  // Denotes if this is a global variable write
  bool global;
  // Denotes if this is a heap write
  bool heap;
  // Denotes if this is a write to a memory location passed in as an input parameter
  bool arg;
  // Denotes if it is heap or arg-specified memory location
  bool heap_or_arg;

};

struct CFGStats {
  // Number of original nodes in the CFG.
  int n_original_nodes;
  // Number of nodes after instrumentation lowering.
  int n_lowered_nodes;
  // Percentage increase of nodes.
  double increase;

  // Number of safe paths (i.e no writes, calls) in the control flow graph.
  int safe_paths;
  // Number of unsafe paths in the control flow graph.
  int unsafe_paths;
  // Percentage of safe control paths in the control flow graph.
  double safe_ratio;

  CFGStats()
      : n_original_nodes(0), n_lowered_nodes(0), increase(0.0), safe_paths(0),
        unsafe_paths(0), safe_ratio(0.0) {}
};

// Strongly connected components in the control flow graph.
struct SCComponent {
  // Blocks belonging to the strongly connected component.
  std::set<Block*> blocks;
  // Return blocks contained within the component if any.
  std::set<Block*> returns;
  // Children of the component.
  std::set<SCComponent*> children;
  // Parents of the component.
  std::set<SCComponent*> parents;
  // Outgoing edges from this component. This maps the target block to the
  // target component. Only features inter component edges.
  std::map<Block*, SCComponent*> outgoing;

  SCComponent() {}

  SCComponent(const SCComponent& sc) {
    blocks = sc.blocks;
    returns = sc.returns;
  }

  SCComponent& operator=(const SCComponent& sc) {
    if (this == &sc)
      return *this;

    blocks = sc.blocks;
    returns = sc.returns;
    return *this;
  }

  ~SCComponent() {}

  // Outgoing edge target component to target block mapping. Only used within
  // anlaysis passes during CFG construction.
  std::map<SCComponent*, Block*> targets;
};

struct StackAccess {
  // Stack height at src operand. INT_MAX if src is not a stack memory access.
  int src;
  // Stack height at dest operand. INT_MAX if dest is not a stack memory access.
  int dest;
};

struct FuncSummary {
  Function* func;

  // Control flow graph of the function. Only initialized for functions with no
  // indirect or unknown control flows.
  SCComponent* cfg;

  // Statistics on instrumentation lowered control flow graph.
  CFGStats* stats;

  // Denotes if this function is safe so that shadow stack checks can be
  // skipped.
  //
  // Safety of a function is defined as that it can be statically guaranteed
  // that it will not write to stack above  its frame. Statically determinable
  // heap writes are considered safe.
  bool safe;

  // Denotes if any exceptional conditions has rendered this function unsafe
  // irrespective of if it writes to memory or not.
  //
  // Currently we have following exceptional conditions:
  //   - Function is too large: We skip static analyses (to keep static analyses
  //       times tractable) and simply assume it is unsafe.
  //   - Function has PLT calls or unknown control flow : Stack mutation effects
  //       due to callees/ indirect control flows cannot be statically
  //       determined so we consider current function to be unsafe as well.
  //
  //  If a function is assumed unsafe at certain point in the analysis pipeline
  //  rest of the analyses in the pipeline can choose to simply skip analysing
  //  the function.
  bool assume_unsafe;

  // Denotes whether this function itself unsafely writes to memory.
  bool self_unsafe_writes;
  // Denotes whether this function's callees unsafely write to memory.
  bool child_writes;
  // Denotes overall if it should be considered that this function will write to
  // memory unsafely due to self writes, child writes or any exceptional
  // condtions.
  bool writes;

  // All memory writes within the function.
  std::map<Address, MemoryWrite*> all_writes;
  // All stack writes within the function keyed by stack offset at write.
  std::map<int, MemoryWrite*> stack_writes;
  // Unsafe basic blocks due to memory writes and calls.
  std::set<Block*> unsafe_blocks;

  // PLT call map. Keyed by the address of the call
  // and the value is the callee's name.
  std::map<Address, std::string> plt_calls;
  // Denotes whether this function has unknown control flows.
  bool has_unknown_cf;
  // Denotes whether this function has indirect control flows;.
  bool has_indirect_cf;

  // Denote whether this function moves down SP to create stack frame
  bool moveDownSP;
  std::set<int> redZoneAccess;

  // Callees of this function.
  std::set<Function*> callees;
  // Callers of this function.
  std::set<Function*> callers;

  // Set of registers dead at function entry.
  std::set<std::string> dead_at_entry;
  // Set of registers dead at each of the function exits.
  std::map<Address, std::set<std::string>> dead_at_exit;
  // Unused registers. Currently only set for leaf functions.
  std::set<std::string> unused_regs;

  std::map<Address, MoveInstData*> entryData;
  std::map<Address, MoveInstData*> exitData;
  std::map<Address, MoveInstData*> entryFixedData;

  // Use the block entry address as the key
  std::map<Address, int> blockEndSPHeight;
  std::map<Address, int> blockEntrySPHeight;

  // All stack memory accesses within the function. Keyed by the instruction
  // address.
  std::map<Address, StackAccess> stack_heights;
  // All unknown memory accesses within basic blocks. Keyed by the ParseAPI::Block pointer
  std::map<Block*, std::set<Address>> unknown_writes;
  // All heap accesses within basic blocks. Keyed by block start addresses.
  std::map<Address, std::set<Address>> heap_writes;
  // All out parameter writes within basic blocks. Keyed by block start
  // addresses.
  std::map<Address, std::set<Address>> arg_writes;
  // All potentially heap or out parameter writes within basic blocks. Keyed by
  // block startaddresses.
  std::map<Address, std::set<Address>> heap_or_arg_writes;

  int safe_paths;

  bool func_exception_safe;

  void Print() {
    printf("Writes to memory = %d ", writes);
    printf("Has PLT calls = %lu ", plt_calls.size());
    printf("Has unknown control flow = %d ", has_unknown_cf);
    printf("Number of callees = %lu\n", callees.size());
  }

  bool shouldUseRegisterFrame() {
    if (callees.size() > 0)
      return false;
    if (unused_regs.size() == 0)
      return false;
    if (has_unknown_cf || !plt_calls.empty())
      return false;

    // If this function creates a stack frame, it may over-write
    // the red-zone location at the function entry
    if (moveDownSP)
      return false;
    auto it = redZoneAccess.begin();
    if (it != redZoneAccess.end()) {
      // We always try to use [-128, -120) range of the red zone.
      // So, if the original code uses any byte in this range,
      // we cannot perform the optimization.
      //
      // Here I assume that if the original code uses a deeper red zone
      // location, it must have used any space shallower in the red zone.
      if (*it < -120)
        return false;
    }
    return true;
  }

  MoveInstData* getMoveInstDataAtEntry(Address a) {
    auto it = entryData.find(a);
    if (it == entryData.end())
      return nullptr;
    return it->second;
  }
  MoveInstData* getMoveInstDataFixedAtEntry(Address a) {
    auto it = entryFixedData.find(a);
    if (it == entryFixedData.end())
      return nullptr;
    return it->second;
  }

  MoveInstData* getMoveInstDataAtExit(Address a) {
    auto it = exitData.find(a);
    if (it == exitData.end())
      return nullptr;
    return it->second;
  }

  bool lowerInstrumentation() {
    if (unsafe_blocks.find(func->entry()) != unsafe_blocks.end())
      return false;
    if (blockEndSPHeight.empty())
      return false;
    if (safe_paths == 0)
      return false;
    return true;
  }

  bool unsafePLTCalls() {
    for (auto it : plt_calls ) {
      if (IsSafePLTCall(it.second)) continue;
      return true;
    }
    return false;
  }
  
  static bool IsSafePLTCall(std::string name) {
    if (name == "malloc") return true;

    // C++ new operators
    if (name.find("_Zna") == 0) return true;

    if (name == "strlen") return true;
    if (name == "strcmp") return true;
    return false;
  }


};

struct PassResult {
  std::string name;
  std::map<std::string, std::string> data;
};

struct AnalysisResult {
  std::vector<PassResult*> pass_results;
};

class Pass {
 public:
  Pass(std::string name, std::string description)
      : pass_name_(name), description_(description) {}

  virtual void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                                PassResult* result) {
    // NOOP
  }

  virtual void RunGlobalAnalysis(CodeObject* co,
                                 std::map<Function*, FuncSummary*>& summaries,
                                 PassResult* result) {
    // NOOP
  }

  virtual bool IsSafeFunction(FuncSummary* s) { return !s->writes; }

  void RunPass(CodeObject* co, std::map<Function*, FuncSummary*>& summaries,
               AnalysisResult& result) {
    if (FLAGS_vv) {
      StdOut(Color::YELLOW) << "------------------------------------" << Endl;
      StdOut(Color::YELLOW) << "Running pass > " << pass_name_ << Endl;
      StdOut(Color::YELLOW) << "  Description : " << description_ << Endl;
    }

    PassResult* pr = new PassResult;
    pr->name = pass_name_;

    result.pass_results.push_back(pr);

    for (auto f : co->funcs()) {
      RunLocalAnalysis(co, f, summaries[f], pr);
    }

    RunGlobalAnalysis(co, summaries, pr);

    long count = 0;
    for (auto f : co->funcs()) {
      FuncSummary* s = summaries[f];
      if (IsSafeFunction(s)) {
        ++count;
      }
    }

    pr->data["Safe Functions"] = count;

    StdOut(Color::YELLOW, FLAGS_vv)
        << "  Safe Functions Found (cumulative) : " << count << Endl;
  }

 protected:
  std::string pass_name_;
  std::string description_;
};

class PassManager {
 public:
  PassManager* AddPass(Pass* pass) {
    passes_.push_back(pass);
    return this;
  }

  std::set<FuncSummary*> Run(CodeObject* co) {
    for (auto f : co->funcs()) {
      FuncSummary* s = summaries_[f];
      if (s == nullptr) {
        s = new FuncSummary();
        s->func = f;
        summaries_[f] = s;
      }
    }

    using ClockType = std::chrono::system_clock;
    auto start = ClockType::now();

    for (Pass* p : passes_) {
      p->RunPass(co, summaries_, result_);
    }

    auto diff = ClockType::now() - start;
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(diff);
    auto elapsed = duration.count();

    std::set<FuncSummary*> s;
    Pass* last = passes_.back();

    std::set<std::string> safe_fns;
    for (auto& it : summaries_) {
      it.second->safe = last->IsSafeFunction(it.second);
      s.insert(it.second);
      if (it.second->safe) {
        safe_fns.insert(it.second->func->name());
      }
    }

    long safe_fn_count = safe_fns.size();
    long unsafe_fn_count = co->funcs().size() - safe_fn_count;

    if (FLAGS_stats != "") {
      // Stats file format:
      //
      // <safe_fn_count>,<unsafe_fn_count>
      //
      // safe_fn_1
      // ..
      // safe_fn_n
      //
      // elapsed (seconds) : <elapsed_time>
      std::ofstream stats;
      stats.open(FLAGS_stats);
      stats << safe_fn_count << "," << unsafe_fn_count << "\n\n";
      for (auto& fn : safe_fns) {
        stats << fn << "\n";
      }

      stats << "\nelapsed (seconds) : " << elapsed;
      stats.close();
    }

    if (FLAGS_vv) {
      StdOut(Color::BLUE) << "Analysis took " << elapsed << " seconds." << Endl;
      StdOut(Color::BLUE) << "\nSummary: " << Endl;
      StdOut(Color::BLUE) << "  Safe Functions Found : " << safe_fn_count
                          << Endl;
      StdOut(Color::BLUE) << "  Non Safe Functions : " << unsafe_fn_count
                          << Endl;
    }
    return s;
  }

  void LogResult(std::ostream& out) {}

 private:
  std::map<Function*, FuncSummary*> summaries_;
  std::vector<Pass*> passes_;
  AnalysisResult result_;
};

#endif  // LITECFI_PASS_MANAGER_H
