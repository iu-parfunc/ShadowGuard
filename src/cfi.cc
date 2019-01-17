
#include "gflags/gflags.h"
#include "glog/logging.h"

// DyninstAPI
#include "BPatch.h"
#include "BPatch_basicBlock.h"
#include "BPatch_flowGraph.h"
#include "BPatch_function.h"
#include "BPatch_point.h"
#include "BPatch_snippet.h"

// PatchAPI
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"

#include "register_usage.h"

DEFINE_string(mode, "back-edge",
              "Level of CFI protection. Valid values are "
              "back-edge, forward-edge, full.");

using namespace Dyninst;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::PatchAPI;

void PrintVector(const std::vector<bool>& vec) {
  for (int i = 0; i < vec.size(); i++) {
    printf("%d", vec[i]);
  }
  printf("\n");
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binaryPathStr(argv[1]);
  Dyninst::SymtabAPI::Symtab* symTab;
  bool isParsable = Dyninst::SymtabAPI::Symtab::openFile(symTab, binaryPathStr);
  if (isParsable == false) {
    const char* error = "error: file can not be parsed";
    std::cout << error;
    return -1;
  }

  // Create a new binary code object from the filename argument
  Dyninst::ParseAPI::SymtabCodeSource* sts =
      new Dyninst::ParseAPI::SymtabCodeSource(argv[1]);
  Dyninst::ParseAPI::CodeObject* co = new Dyninst::ParseAPI::CodeObject(sts);
  co->parse();

  RegisterUsageInfo info = FindUnusedRegisterInfo(co);
  PrintVector(info.unused_avx_mask);
  PrintVector(info.unused_mmx_mask);

  return 0;
}
