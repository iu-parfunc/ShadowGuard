#ifndef LITECFI_PASS_MANAGER_H_
#define LITECFI_PASS_MANAGER_H_

#include <fstream>
#include <map>
#include <set>
#include <string>

#include "CodeObject.h"
#include "gflags/gflags.h"
#include "utils.h"

using namespace Dyninst;
using namespace Dyninst::ParseAPI;

DECLARE_bool(vv);
DECLARE_string(stats);

struct FuncSummary {
  Function* func;

  bool safe;

  bool writesMemory;
  bool adjustSP;
  bool containsPLTCall;
  bool containsUnknownCF;
  // Callees of this function.
  std::set<Function*> callees;
  // Callers of this function.
  std::set<Function*> callers;
  // If this function is at a loop head.
  bool loop_head;
  // Set of registers dead at function entry.
  std::set<std::string> dead_at_entry;
  // Set of registers dead at each of the function exits.
  std::map<Address, std::set<std::string>> dead_at_exit;
  // Unused caller saved registers. Currently only set for leaf functions.
  std::set<std::string> unused_caller_saved;

  void Print() {
    printf("writesMemory=%d ", writesMemory);
    printf("adjustSP=%d ", adjustSP);
    printf("containsPLTCall=%d ", containsPLTCall);
    printf("containsUnknownCF=%d ", containsUnknownCF);
    printf("Callee=%lu\n", callees.size());
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

  virtual bool IsSafeFunction(FuncSummary* s) {
    return !s->writesMemory && !s->adjustSP && !s->containsPLTCall &&
           !s->containsUnknownCF;
  }

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

    for (Pass* p : passes_) {
      p->RunPass(co, summaries_, result_);
    }

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
      std::ofstream stats;
      stats.open(FLAGS_stats);
      stats << safe_fn_count << "," << unsafe_fn_count << "\n\n";
      for (auto& fn : safe_fns) {
        stats << fn << "\n";
      }
      stats.close();
    }

    if (FLAGS_vv) {
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
