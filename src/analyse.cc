#include "Absloc.h"
#include "AbslocInterface.h"
#include "CFG.h"
#include "CodeObject.h"
#include "CodeSource.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Register.h"
#include "pass_manager.h"
#include "passes.h"

using namespace std;
using namespace Dyninst;
using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::DataflowAPI;

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

  PassManager *pm = new PassManager;
  pm->AddPass(new LeafAnalysisPass())
      ->AddPass(new StackAnalysisPass())
      ->AddPass(new NonLeafSafeWritesPass());
  std::vector<Function *> safe = pm->Run(co);
  return 0;
}
