#ifndef LITECFI_PASSES_H_
#define LITECFI_PASSES_H_

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
#include "liveness.h"
#include "pass_manager.h"
#include "utils.h"

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
          s->containsUnknownCF = true;
          continue;
        }
        if (e->type() != CALL)
          continue;
        if (co->cs()->linkage().find(e->trg()->start()) !=
            co->cs()->linkage().end()) {
          s->containsPLTCall = true;
          continue;
        }
        std::vector<Function*> funcs;
        e->trg()->getFuncs(funcs);
        Function* callee = NULL;
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

class LeafAnalysisPass : public Pass {
 public:
  LeafAnalysisPass()
      : Pass("Leaf Analysis",
             "Skips leaf functions that does not write memory or "
             "adjust SP.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;

    for (auto b : f->blocks()) {
      b->getInsns(insns);

      for (auto const& ins : insns) {
        // Call instructions always write to memory.
        // But they create new frames, so the writes are ignored.
        bool isCall =
            (ins.second.getCategory() == Dyninst::InstructionAPI::c_CallInsn);
        if (isCall)
          continue;
        s->writesMemory |= ins.second.writesMemory();

        std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;
        ins.second.getWriteSet(written);

        bool isRet =
            (ins.second.getCategory() == Dyninst::InstructionAPI::c_ReturnInsn);
        if (isRet)
          continue;
        for (auto const& written_register : written) {
          if (written_register->getID().isStackPointer())
            s->adjustSP = true;
        }
      }
    }
  }

  bool IsSafeFunction(FuncSummary* s) override {
    return !s->writesMemory && !s->adjustSP && !s->containsPLTCall &&
           !s->containsUnknownCF && s->callees.empty();
  }
};

class StackAnalysisPass : public Pass {
 public:
  StackAnalysisPass()
      : Pass("Stack Analysis",
             "Skips leaf functions that do not write unknown memory or "
             "adjust SP.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s,
                        PassResult* result) override {
    s->writesMemory = false;
    // If a function adjust SP in a weird way
    // so that Dyninst's stack analyasis cannot determine where
    // it SP is, then stack writes will become unknown location
    // Therefore, we do not use this any more, and fully
    // rely on Dyninst's stack analysis.
    s->adjustSP = false;

    AssignmentConverter converter(true /* cache results*/,
                                  true /* use stack analysis*/);

    std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;
    for (auto b : f->blocks()) {
      b->getInsns(insns);

      for (auto const& ins : insns) {
        if (ins.second.writesMemory()) {
          // Assignment is essentially SSA.
          // So, an instruction may have multiple SSAs.
          std::vector<Assignment::Ptr> assigns;
          converter.convert(ins.second, ins.first, f, b, assigns);
          for (auto a : assigns) {
            AbsRegion& out = a->out();  // The lhs.
            Absloc loc = out.absloc();
            switch (loc.type()) {
            case Absloc::Stack:
              // If the stack location is in the previous frame,
              // it is a dangerous write.
              if (loc.off() >= 0)
                s->writesMemory = true;
              break;
            case Absloc::Unknown:
              // Unknown memory writes.
              s->writesMemory = true;
              break;
            case Absloc::Heap:
              // This is actually a write to a global varible.
              // That's why it will have a statically determined address.
              break;
            case Absloc::Register:
              // Not a memory write.
              break;
            }
          }
        }
      }
    }
  }

  bool IsSafeFunction(FuncSummary* s) override {
    return !s->writesMemory && !s->adjustSP && !s->containsPLTCall &&
           !s->containsUnknownCF && s->callees.empty();
  }
};

class NonLeafSafeWritesPass : public Pass {
 public:
  NonLeafSafeWritesPass()
      : Pass("Non Leaf Safe Writes",
             "Skips functions that itself and its callee do not write "
             "unknown memory or adjust SP.") {}

  void RunGlobalAnalysis(CodeObject* co,
                         std::map<Function*, FuncSummary*>& summaries,
                         PassResult* result) override {
    for (auto f : co->funcs()) {
      FuncSummary* s = summaries[f];
      // Make conservative assumptions about PLT callees
      // and indirect callees.
      s->writesMemory =
          s->writesMemory || s->containsPLTCall || s->containsUnknownCF;
      s->adjustSP = s->adjustSP || s->containsPLTCall || s->containsUnknownCF;
    }

    bool done = false;
    while (!done) {
      done = true;
      for (auto f : co->funcs()) {
        FuncSummary* s = summaries[f];

        FuncSummary new_s;
        new_s.writesMemory = s->writesMemory;
        new_s.adjustSP = s->adjustSP;

        for (auto tarf : s->callees) {
          FuncSummary* cs = summaries[tarf];
          new_s.writesMemory |= cs->writesMemory;
          new_s.adjustSP |= cs->adjustSP;
        }

        if (new_s.adjustSP != s->adjustSP ||
            new_s.writesMemory != s->writesMemory) {
          s->adjustSP = new_s.adjustSP;
          s->writesMemory = new_s.writesMemory;
          done = false;
        }
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
    // Construct a liveness analyzer based on the address width of the mutatee.
    // 32bit code and 64bit code have different ABI.
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
    s->dead_at_entry =
        GetDeadRegisters(f, f->entry(), LivenessAnalyzer::Before);

    for (auto b : f->exitBlocks()) {
      s->dead_at_exit[b->end()] =
          GetDeadRegisters(f, b, LivenessAnalyzer::After);
    }
  }
};

#endif  // LITECFI_PASSES_H
