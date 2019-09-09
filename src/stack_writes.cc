#include "Absloc.h"
#include "AbslocInterface.h"
#include "CFG.h"
#include "CodeObject.h"
#include "CodeSource.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Register.h"

using namespace std;
using namespace Dyninst;
using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::DataflowAPI;

struct FuncSummary {
  bool writesMemory;
  bool adjustSP;
  bool containsPLTCall;
  bool containsUnknownCF;
  std::set<Function *> callees;

  bool skipInstrumentation() {
    // We skip instrumentation for leaf functions
    // that do not write memory or adjust SP
    return !writesMemory && !adjustSP && !containsPLTCall &&
           !containsUnknownCF && callees.empty();
  }
  void Print() {
    printf("writesMemory=%d ", writesMemory);
    printf("adjustSP=%d ", adjustSP);
    printf("containsPLTCall=%d ", containsPLTCall);
    printf("containsUnknownCF=%d ", containsUnknownCF);
    printf("Callee=%d\n", callees.size());
  }
};

struct InterProcSummary {
  bool writesMemory;
  bool adjustSP;

  bool skipInstrumentation() { return !writesMemory && !adjustSP; }

  bool operator!=(const InterProcSummary &rhs) const {
    return (writesMemory != rhs.writesMemory) || (adjustSP != rhs.adjustSP);
  }
};

map<Function *, FuncSummary> funcSummary;
map<Function *, InterProcSummary> interProcSummary;
map<Function *, FuncSummary> stackAnalysisSummary;

void CalculateFunctionSummary(CodeObject *co, Function *f) {
  FuncSummary &s = funcSummary[f];

  std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;

  for (auto b : f->blocks()) {
    b->getInsns(insns);

    for (auto const &ins : insns) {
      // Call instructions always write to memory
      // But they create new frames, so the writes are ignored
      bool isCall =
          (ins.second.getCategory() == Dyninst::InstructionAPI::c_CallInsn);
      if (isCall)
        continue;
      s.writesMemory |= ins.second.writesMemory();

      std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;
      ins.second.getWriteSet(written);

      bool isRet =
          (ins.second.getCategory() == Dyninst::InstructionAPI::c_ReturnInsn);
      if (isRet)
        continue;
      for (auto const &written_register : written) {
        if (written_register->getID().isStackPointer())
          s.adjustSP = true;
      }
    }

    for (auto e : b->targets()) {
      if (e->sinkEdge() && e->type() != RET) {
        s.containsUnknownCF = true;
        continue;
      }
      if (e->type() != CALL)
        continue;
      if (co->cs()->linkage().find(e->trg()->start()) !=
          co->cs()->linkage().end()) {
        s.containsPLTCall = true;
        continue;
      }
      std::vector<Function *> funcs;
      e->trg()->getFuncs(funcs);
      Function *callee = NULL;
      for (auto call_trg : funcs) {
        if (call_trg->entry() == e->trg()) {
          callee = call_trg;
          break;
        }
      }
      if (callee)
        s.callees.insert(callee);
    }
  }
}

void FunctionSummaryPropagation(CodeObject *co) {
  for (auto f : co->funcs()) {
    InterProcSummary &ips = interProcSummary[f];
    const FuncSummary &s = stackAnalysisSummary[f];
    // Make conservative assumptions about PLT callees
    // and indirect callees
    ips.writesMemory =
        s.writesMemory || s.containsPLTCall || s.containsUnknownCF;
    ips.adjustSP = s.adjustSP || s.containsPLTCall || s.containsUnknownCF;
  }

  bool done = false;
  while (!done) {
    done = true;
    for (auto f : co->funcs()) {
      InterProcSummary newFact = interProcSummary[f];
      const FuncSummary &s = funcSummary[f];
      for (auto tarf : s.callees) {
        const InterProcSummary &ips = interProcSummary[tarf];
        newFact.writesMemory |= ips.writesMemory;
        newFact.adjustSP |= ips.adjustSP;
      }
      if (newFact != interProcSummary[f]) {
        interProcSummary[f] = newFact;
        done = false;
      }
    }
  }
}

void PerformStackAnalysis(Function *f) {
  FuncSummary &s = stackAnalysisSummary[f];
  s = funcSummary[f];
  s.writesMemory = false;
  // If a function adjust SP in a weird way
  // so that Dyninst's stack analyasis cannot determine where
  // it SP is, then stack writes will become unknown location
  // Therefore, we do not use this any more, and fully
  // rely on Dyninst's stack analysis.
  s.adjustSP = false;

  AssignmentConverter converter(true /* cache results*/,
                                true /* use stack analysis*/);

  std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;
  for (auto b : f->blocks()) {
    b->getInsns(insns);

    for (auto const &ins : insns) {
      if (ins.second.writesMemory()) {
        // Assignment is essentially SSA
        // So, an instruction may have multiple SSAs
        std::vector<Assignment::Ptr> assigns;
        converter.convert(ins.second, ins.first, f, b, assigns);
        for (auto a : assigns) {
          AbsRegion &out = a->out();  // The lhs
          Absloc loc = out.absloc();
          switch (loc.type()) {
          case Absloc::Stack:
            // If the stack location is in the previous frame,
            // it is a dangerous write
            if (loc.off() >= 0)
              s.writesMemory = true;
            break;
          case Absloc::Unknown:
            // Unknown memory writes
            s.writesMemory = true;
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

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(
        stderr,
        "Usage: %s <binary>\nAnalyze writes to stack and adjustment to SP\n",
        argv[0]);
    exit(-1);
  }

  SymtabCodeSource *sts;
  CodeObject *co;

  sts = new SymtabCodeSource(argv[1]);
  co = new CodeObject(sts);
  co->parse();

  for (auto f : co->funcs()) {
    CalculateFunctionSummary(co, f);
  }

  printf("Analysis 1: only skip leaf function that does not write memory or "
         "adjust SP\n");
  int count = 0;
  for (auto f : co->funcs()) {
    FuncSummary &s = funcSummary[f];
    if (s.skipInstrumentation()) {
      // printf("%s at %lx\n", f->name().c_str(), f->addr());
      ++count;
    }
  }
  printf("%d functions skipped\n\n", count);

  printf("Analysis 2: skip leaf function that do not write unknown memory or "
         "adjust SP\n");
  for (auto f : co->funcs()) {
    PerformStackAnalysis(f);
  }
  count = 0;
  for (auto f : co->funcs()) {
    FuncSummary &s = stackAnalysisSummary[f];
    if (s.skipInstrumentation()) {
      // printf("%s at %lx\n", f->name().c_str(), f->addr());
      ++count;
    }
  }
  printf("%d functions skipped\n\n", count);

  FunctionSummaryPropagation(co);
  printf("Analysis 3: skip function that itself and its callee do not write "
         "unknown memory or adjust SP\n");
  count = 0;
  for (auto f : co->funcs()) {
    InterProcSummary &s = interProcSummary[f];
    if (s.skipInstrumentation()) {
      // printf("%s at %lx\n", f->name().c_str(), f->addr());
      ++count;
    }
  }
  printf("%d functions skipped\n\n", count);
  count = 0;
  for (auto f : co->funcs()) {
    InterProcSummary &s = interProcSummary[f];
    if (!s.skipInstrumentation()) {
      // printf("%s\n", f->name().c_str());
      count++;
    }
  }
  printf("Instrument %d functions\n", count);
}
