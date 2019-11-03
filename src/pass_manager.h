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

struct MemoryWrite {
  MemoryWrite() {}

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
};

struct FuncSummary {
  Function* func;

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
  bool self_writes;
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

  // Denotes whether this function calls PLT functions.
  bool has_plt_call;
  // Denotes whether this function has unknown control flows.
  bool has_unknown_cf;

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

  void Print() {
    printf("Writes to memory = %d ", writes);
    printf("Has PLT calls = %d ", has_plt_call);
    printf("Has unknown control flow = %d ", has_unknown_cf);
    printf("Number of callees = %lu\n", callees.size());
  }

  bool shouldUseRegisterFrame() {
      if (callees.size() > 0) return false;
      if (unused_regs.size() == 0) return false;
      if (has_unknown_cf || has_plt_call) return false;

      // If this function creates a stack frame, it may over-write
      // the red-zone location at the function entry
      if (moveDownSP) return false;
      auto it = redZoneAccess.begin();
      if (it != redZoneAccess.end()) {
          // We always try to use [-128, -120) range of the red zone.
          // So, if the original code uses any byte in this range,
          // we cannot perform the optimization.
          //
          // Here I assume that if the original code uses a deeper red zone location,
          // it must have used any space shallower in the red zone.
          if (*it < -120) return false;
      }
      return true;
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
