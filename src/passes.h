#ifndef LITECFI_PASSES_H_
#define LITECFI_PASSES_H_

#include <algorithm>

#include "Absloc.h"
#include "AbslocInterface.h"
#include "CFG.h"
#include "CodeObject.h"
#include "CodeSource.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Location.h"
#include "Register.h"
#include "bitArray.h"
#include "glog/logging.h"
#include "liveness.h"
#include "pass_manager.h"
#include "utils.h"

using Dyninst::Absloc;
using Dyninst::AbsRegion;
using Dyninst::Address;
using Dyninst::Offset;
using Dyninst::InstructionAPI::Instruction;
using Dyninst::InstructionAPI::RegisterAST;
using Dyninst::ParseAPI::Block;
using Dyninst::ParseAPI::Function;
using Dyninst::ParseAPI::Location;

class CallGraphPass : public Pass {
 public:
  CallGraphPass()
      : Pass("Call Graph Generation", "Generates the application call graph.") {
  }

  void RunGlobalAnalysis(CodeObject* co,
                         std::map<Function*, FuncSummary*>& summaries,
                         PassResult* result) override {
    std::set<Function*> visited;
    for (auto f : co->funcs()) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        VisitFunction(co, summaries[f], summaries, visited);
      }
    }
  }

 private:
  void UpdateCallees(CodeObject* co, Function* f, FuncSummary* s) {
    for (auto b : f->blocks()) {
      for (auto e : b->targets()) {
        if (e->sinkEdge() && e->type() != RET) {
          s->has_unknown_cf = true;
          s->assume_unsafe = true;
          continue;
        }
        if (e->type() != CALL)
          continue;
        if (co->cs()->linkage().find(e->trg()->start()) !=
            co->cs()->linkage().end()) {
          s->has_plt_call = true;
          s->assume_unsafe = true;
          continue;
        }
        std::vector<Function*> funcs;
        e->trg()->getFuncs(funcs);
        Function* callee = nullptr;
        for (auto call_trg : funcs) {
          if (call_trg->entry() == e->trg()) {
            callee = call_trg;
            break;
          }
        }
        if (callee)
          s->callees.insert(callee);
      }
    }
  }

  void VisitFunction(CodeObject* co, FuncSummary* s,
                     std::map<Function*, FuncSummary*>& summaries,
                     std::set<Function*>& visited) {
    visited.insert(s->func);
    UpdateCallees(co, s->func, s);
    for (auto f : s->callees) {
      summaries[f]->callers.insert(s->func);
      auto it = visited.find(f);
      if (it == visited.end()) {
        VisitFunction(co, summaries[f], summaries, visited);
      }
    }
  }
};

class LargeFunctionFilterPass : public Pass {
 public:
  LargeFunctionFilterPass()
      : Pass("Large Function Filter",
             "Filters out large functions from static anlaysis.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe)
      return;

    Address start = f->addr();
    Address end = 0;
    for (auto b : f->exitBlocks()) {
      end = std::max(end, b->end());
    }

    for (auto b : f->returnBlocks()) {
      end = std::max(end, b->end());
    }

    if (end < start) {
      return;
    }

    if (end - start > kCutoffSize_) {
      StdOut(Color::RED, FLAGS_vv)
          << "    Skipping large function " << f->name()
          << " with size : " << end - start << Endl;

      // Bail out and conservatively assume the function is unsafe.
      s->assume_unsafe = true;
    }
  }

  static constexpr unsigned int kCutoffSize_ = 20000;
};

class IntraProceduralMemoryAnalysis : public Pass {
 public:
  IntraProceduralMemoryAnalysis()
      : Pass("Intra-procedural Memory Write and Stack Pointer Overwriee "
             "Analysis",
             "Analyzes the memory writes within a function. Also detects stack"
             " pointer overwrites.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe)
      return;

    AssignmentConverter converter(true /* cache results*/,
                                  true /* use stack analysis*/);
    std::map<Offset, Instruction> insns;

    for (auto b : f->blocks()) {
      insns.clear();
      b->getInsns(insns);

      for (auto const& ins : insns) {
        // Ignore writes due to frame switching instructions such as call/ ret.
        if (IsFrameSwitchingInstruction(ins.second))
          continue;

        if (ins.second.writesMemory()) {
          // Assignment is essentially SSA.
          // So, an instruction may have multiple SSAs.
          std::vector<Assignment::Ptr> assigns;
          converter.convert(ins.second, ins.first, f, b, assigns);

          MemoryWrite* write = new MemoryWrite;
          write->function = f;
          write->block = b;
          write->ins = ins.second;
          write->addr = ins.first;

          for (auto a : assigns) {
            AbsRegion& out = a->out();  // The lhs.
            Absloc loc = out.absloc();
            switch (loc.type()) {
            case Absloc::Stack: {
              // If the stack location is in the previous frame,
              // it is a dangerous write.
              write->stack = true;
              s->stack_writes[loc.off()] = write;
              if (loc.off() >= -8) {
                s->assume_unsafe = true;
              }
              break;
            }
            case Absloc::Unknown:
              // Unknown memory writes.
              s->self_writes = true;
              break;
            case Absloc::Heap:
              // This is actually a write to a global variable.
              // That's why it will have a statically determined address.
              break;
            case Absloc::Register:
              // Not a memory write.
              break;
            }
          }

          s->all_writes[write->addr] = write;
        }
      }
    }
  }

  bool IsSafeFunction(FuncSummary* s) override {
    return !s->self_writes && !s->assume_unsafe && s->callees.empty();
  }

 private:
  bool IsFrameSwitchingInstruction(const Instruction& ins) {
    // Call or return instructions switch frames.
    if (ins.getCategory() == Dyninst::InstructionAPI::c_CallInsn ||
        ins.getCategory() == Dyninst::InstructionAPI::c_ReturnInsn) {
      return true;
    }
    return false;
  }
};

class InterProceduralMemoryAnalysis : public Pass {
 public:
  InterProceduralMemoryAnalysis()
      : Pass("Inter-procedural Memory Write Analysis",
             "Analyses memory writes across functions.") {}

  void RunGlobalAnalysis(CodeObject* co,
                         std::map<Function*, FuncSummary*>& summaries,
                         PassResult* result) override {
    std::set<Function*> visited;
    for (auto f : co->funcs()) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        VisitFunction(co, summaries[f], summaries, visited);
      }
    }
  }

 private:
  bool VisitFunction(CodeObject* co, FuncSummary* s,
                     std::map<Function*, FuncSummary*>& summaries,
                     std::set<Function*>& visited) {
    visited.insert(s->func);

    for (auto f : s->callees) {
      auto it = visited.find(f);
      if (it == visited.end()) {
        s->child_writes |= VisitFunction(co, summaries[f], summaries, visited);
      } else {
        s->child_writes |= summaries[f]->writes;
      }
    }

    s->writes = s->self_writes || s->child_writes || s->assume_unsafe;
    return s->writes;
  }
};

class UnusedRegisterAnalysisPass : public Pass {
 public:
  UnusedRegisterAnalysisPass()
      : Pass("Unused Register Analysis", "Analyses register unused "
                                         "saved registers of leaf functions.") {
  }

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe) {
      return;
    }

    if (s->callees.size() > 0) {
      return;
    }

    std::set<std::string> all = {
        "x86_64::rax", "x86_64::rbx", "x86_64::rcx", "x86_64::rdx",
        "x86_64::rsi", "x86_64::rdi", "x86_64::r8",  "x86_64::r9",
        "x86_64::r10", "x86_64::r11", "x86_64::r12", "x86_64::r13",
        "x86_64::r14", "x86_64::r15",
    };

    std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;
    std::set<std::string> used;
    for (auto b : f->blocks()) {
      b->getInsns(insns);

      for (auto const& ins : insns) {
        std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> read;
        std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;

        ins.second.getReadSet(read);
        ins.second.getWriteSet(written);

        for (auto const& r : read) {
          used.insert(r->getID().name());
        }

        for (auto const& w : written) {
          used.insert(w->getID().name());
        }
      }
    }

    for (auto r : all) {
      auto it = used.find(r);
      if (it == used.end()) {
        s->unused_regs.insert(r);
      }
    }
  }
};

class DeadRegisterAnalysisPass : public Pass {
 public:
  DeadRegisterAnalysisPass()
      : Pass("Dead Register Analysis",
             "Analyses dead registers at function entry and exit.") {}

  std::set<std::string> GetDeadRegisters(Function* f, Block* b,
                                         LivenessAnalyzer::Type type) {
    // Construct a liveness analyzer based on the address width of the
    // mutatee. 32bit code and 64bit code have different ABI.
    LivenessAnalyzer la(f->obj()->cs()->getAddressWidth());
    // Construct a liveness query location.
    Location loc(f, b);

    std::set<std::string> dead;

    // Query live registers.
    bitArray live;
    if (!la.query(loc, type, live)) {
      return dead;
    }

    // Check all dead caller-saved registers.
    std::vector<MachRegister> regs;
    regs.push_back(x86_64::rsi);
    regs.push_back(x86_64::rdi);
    regs.push_back(x86_64::rdx);
    regs.push_back(x86_64::rcx);
    regs.push_back(x86_64::r8);
    regs.push_back(x86_64::r9);
    regs.push_back(x86_64::r10);
    regs.push_back(x86_64::r11);

    for (auto reg : regs) {
      if (!live.test(la.getIndex(reg))) {
        dead.insert(reg.name());
      }
    }
    return dead;
  }

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    if (s->assume_unsafe) {
      return;
    }

    s->dead_at_entry =
        GetDeadRegisters(f, f->entry(), LivenessAnalyzer::Before);

    for (auto b : f->exitBlocks()) {
      s->dead_at_exit[b->end()] =
          GetDeadRegisters(f, b, LivenessAnalyzer::After);
    }
  }
};

#endif  // LITECFI_PASSES_H
