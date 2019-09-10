#ifndef LITECFI_PASSES_H_
#define LITECFI_PASSES_H_

#include "pass_manager.h"

class LeafAnalysisPass : public Pass {
 public:
  LeafAnalysisPass()
      : Pass("Leaf Analysis",
             "Skips leaf functions that does not write memory or "
             "adjust SP.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s) override {
    std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;

    for (auto b : f->blocks()) {
      b->getInsns(insns);

      for (auto const& ins : insns) {
        // Call instructions always write to memory
        // But they create new frames, so the writes are ignored
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
};

class StackAnalysisPass : public Pass {
 public:
  StackAnalysisPass()
      : Pass("Stack Analysis",
             "Skips leaf functions that do not write unknown memory or "
             "adjust SP.") {}

  void RunLocalAnalysis(CodeObject* co, Function* f, FuncSummary* s) override {
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
          // Assignment is essentially SSA
          // So, an instruction may have multiple SSAs
          std::vector<Assignment::Ptr> assigns;
          converter.convert(ins.second, ins.first, f, b, assigns);
          for (auto a : assigns) {
            AbsRegion& out = a->out();  // The lhs
            Absloc loc = out.absloc();
            switch (loc.type()) {
            case Absloc::Stack:
              // If the stack location is in the previous frame,
              // it is a dangerous write
              if (loc.off() >= 0)
                s->writesMemory = true;
              break;
            case Absloc::Unknown:
              // Unknown memory writes
              s->writesMemory = true;
              break;
            case Absloc::Heap:
              // This is actually a write to a global varible.
              // That's why it will have a statically determined address.
              break;
            case Absloc::Register:
              // Not a memory write
              break;
            }
          }
        }
      }
    }
  }
};

class NonLeafSafeWritesPass : public Pass {
 public:
  NonLeafSafeWritesPass()
      : Pass("Non Leaf Safe Writes",
             "Skips functions that itself and its callee do not write "
             "unknown memory or adjust SP.") {}

  virtual void
  RunGlobalAnalysis(CodeObject* co,
                    std::map<Function*, FuncSummary*>& summaries) override {
    for (auto f : co->funcs()) {
      FuncSummary* s = summaries[f];
      // Make conservative assumptions about PLT callees
      // and indirect callees
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

        if (new_s.adjustSP != s->adjustSP &&
            new_s.writesMemory != s->writesMemory) {
          s->adjustSP = new_s.adjustSP;
          s->writesMemory = new_s.writesMemory;
          done = false;
        }
      }
    }
  }

  virtual bool IsSafeFunction(FuncSummary* s) {
    return !s->writesMemory && !s->adjustSP;
  }
};

#endif  // LITECFI_PASSES_H
