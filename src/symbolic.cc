
#include <iostream>
#include <string>

#include "CodeObject.h"
#include "DynAST.h"
#include "Expression.h"
#include "Graph.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Operand.h"
#include "Register.h"
#include "SymEval.h"
#include "Visitor.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "pass_manager.h"
#include "passes.h"
#include "slicing.h"

using namespace Dyninst;
using namespace ParseAPI;
using namespace InstructionAPI;
using namespace DataflowAPI;

using std::string;

DEFINE_bool(vv, false, "Log verbose output.");
DEFINE_string(stats, "", "File to log statistics related to static anlaysis.");

/**
   0x00000000000006aa <+0>:	push   %rbp
   0x00000000000006ab <+1>:	mov    %rsp,%rbp
   0x00000000000006ae <+4>:	mov    %rdi,-0x8(%rbp)
   0x00000000000006b2 <+8>:	mov    -0x8(%rbp),%rax
   0x00000000000006b6 <+12>:	movl   $0x2a,(%rax)
   0x00000000000006bc <+18>:	nop
   0x00000000000006bd <+19>:	pop    %rbp
   0x00000000000006be <+20>:	retq
**/

class SimpleAssignmentPred : public Slicer::Predicates {
 public:
  SimpleAssignmentPred() {}

  bool modifyCurrentFrame(Slicer::SliceFrame &frame, Graph::Ptr g, Slicer *s) {
    std::cout << "Updating slice graph\n";
  }
};

FuncSummary *GetSummary(const std::set<FuncSummary *> &summaries,
                        string function) {
  for (auto s : summaries) {
    if (s->func->name() == function) {
      return s;
    }
  }

  return nullptr;
}

CodeObject *GetCodeObject(const char *binary) {
  SymtabCodeSource *sts = new SymtabCodeSource(const_cast<char *>(binary));
  CodeObject *co = new CodeObject(sts);
  co->parse();
  return co;
}

PassManager *GetPassManager() {
  PassManager *pm = new PassManager;
  pm->AddPass(new CallGraphAnalysis())
      ->AddPass(new LargeFunctionFilter())
      ->AddPass(new IntraProceduralMemoryAnalysis())
      ->AddPass(new InterProceduralMemoryAnalysis())
      ->AddPass(new DeadRegisterAnalysis());
  return pm;
}

std::set<FuncSummary *> Analyse(string binary) {
  PassManager *pm = GetPassManager();
  return pm->Run(GetCodeObject(binary.c_str()));
}

bool Resolve(MemoryWrite *r, FuncSummary *summary) {
  AssignmentConverter ac(true, true);
  std::vector<Assignment::Ptr> assignments;
  ac.convert(r->ins, r->addr, r->function, r->block, assignments);

  CHECK(assignments.size() == 1);

  std::cout << "Slicing instruction at " << std::hex << r->addr << "\n";
  Slicer s(assignments[0], r->block, r->function);

  SimpleAssignmentPred pred;
  GraphPtr slice = s.backwardSlice(pred);

  Result_t result;
  if (SymEval::expand(slice, result) != SymEval::SUCCESS) {
    std::cout << "Failed expanding the AST.\n";
    return false;
  }

  AST::Ptr expr = result[assignments[0]];
  std::cout << expr->format();

  return true;
}

int main(int argc, char *argv[]) {
  google::InitGoogleLogging(argv[0]);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);
  string function = "simple_write";
  string reg = "x86_64::rax";

  auto summary = GetSummary(Analyse(binary), function);
  CHECK(summary != nullptr);
  CHECK(summary->all_writes.size() == 3);
  CHECK(summary->stack_writes.size() == 2);

  for (auto it : summary->all_writes) {
    if (!it.second->stack) {
      MemoryWrite *r = it.second;

      if (!Resolve(r, summary)) {
        std::cout << "Unable to resolve memory write : " << r->ins.format()
                  << "\n";
        exit(-1);
      }
    }
  }
}
