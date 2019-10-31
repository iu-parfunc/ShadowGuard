
#include <iostream>
#include <string>

#include "CFG.h"
#include "CodeObject.h"
#include "CodeSource.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Register.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

using namespace Dyninst;
using namespace ParseAPI;
using namespace InstructionAPI;

using std::string;

using Dyninst::ParseAPI::CodeObject;
using Dyninst::ParseAPI::SymtabCodeSource;

CodeObject *GetCodeObject(const char *binary) {
  SymtabCodeSource *sts = new SymtabCodeSource(const_cast<char *>(binary));
  CodeObject *co = new CodeObject(sts);
  co->parse();
  return co;
}

int main(int argc, char *argv[]) {
  google::InitGoogleLogging(argv[0]);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);
  string function = "foo";

  auto co = GetCodeObject(binary.c_str());
  Function *func = nullptr;
  for (auto f : co->funcs()) {
    func = f;
    break;
  }

  DCHECK(func != nullptr);

  int n_blocks = 0;
  for (auto b : func->blocks()) {
    n_blocks++;

    for (auto e : b->targets()) {
      if (e->sinkEdge() && e->type() != RET) {
        std::cout << "Unknown control flow found.\n";
        continue;
      }

      if (co->cs()->linkage().find(e->trg()->start()) !=
          co->cs()->linkage().end()) {
        std::cout << "PLT calls found.\n";
        continue;
      }

      if (e->type() == INDIRECT) {
        std::cout << "Indirect control flow found.\n";
        continue;
      }

      if (e->type() == CALL_FT) {
        std::cout << "Call fall through found.\n";
        continue;
      }

      if (e->type() == COND_TAKEN) {
        std::cout << "Conditional taken found.\n";
        continue;
      }

      if (e->type() == COND_NOT_TAKEN) {
        std::cout << "Conditional not taken found.\n";
        continue;
      }

      if (e->type() == CALL) {
        std::vector<Function *> funcs;
        e->trg()->getFuncs(funcs);
        for (auto f : funcs) {
          if (f->entry() == e->trg()) {
            std::cout << "Call too function " << f->name() << " found.\n";
            break;
          }
        }
        continue;
      }

      if (e->type() == RET) {
        std::cout << "Return found.\n";
      }
    }
  }

  std::cout << "Number of basic blocks : " << n_blocks << "\n";
}
