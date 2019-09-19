#ifndef LITECFI_PASS_MANAGER_H_
#define LITECFI_PASS_MANAGER_H_

#include <map>
#include <set>
#include <string>

#include "CodeObject.h"
#include "utils.h"

using namespace Dyninst;
using namespace Dyninst::ParseAPI;

struct FuncSummary {
  Function* func;

  bool safe;

  bool writesMemory;
  bool adjustSP;
  bool containsPLTCall;
  bool containsUnknownCF;
  std::set<Function*> callees;
  // Set of registers dead at function entry.
  std::set<std::string> dead_at_entry;
  // Set of registers dead at each of the function exits.
  std::map<Block*, std::set<std::string>> dead_at_exit;
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
           !s->containsUnknownCF && s->callees.empty();
  }

  void RunPass(CodeObject* co, std::map<Function*, FuncSummary*>& summaries,
               AnalysisResult& result) {
    StdOut(Color::YELLOW) << "------------------------------------" << Endl;
    StdOut(Color::YELLOW) << "Running pass > " << pass_name_ << Endl;
    StdOut(Color::YELLOW) << "  Description : " << description_ << Endl;

    PassResult* pr = new PassResult;
    pr->name = pass_name_;

    result.pass_results.push_back(pr);

    for (auto f : co->funcs()) {
      FuncSummary* s = summaries[f];
      if (s == nullptr) {
        s = new FuncSummary();
        s->func = f;
        summaries[f] = s;
      }
      RunLocalAnalysis(co, f, s, pr);
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

    StdOut(Color::YELLOW) << "  Safe Functions Found (cumulative) : " << count
                          << Endl;
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
    for (Pass* p : passes_) {
      p->RunPass(co, summaries_, result_);
    }

    std::set<FuncSummary*> s;
    Pass* last = passes_.back();
    long safe_count = 0;
    for (auto& it : summaries_) {
      it.second->safe = last->IsSafeFunction(it.second);
      s.insert(it.second);
      if (it.second->safe) {
        safe_count++;
      }
    }

    StdOut(Color::BLUE) << "\nSummary: " << Endl;
    StdOut(Color::BLUE) << "  Safe Functions Found : " << safe_count << Endl;
    StdOut(Color::BLUE) << "  Non Safe Functions : "
                        << co->funcs().size() - safe_count << Endl;
    return s;
  }

  void LogResult(std::ostream& out) {}

 private:
  std::map<Function*, FuncSummary*> summaries_;
  std::vector<Pass*> passes_;
  AnalysisResult result_;
};

#endif  // LITECFI_PASS_MANAGER_H
