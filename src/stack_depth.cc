
#include "BPatch.h"
#include "BPatch_basicBlock.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "BPatch_point.h"
#include "InstSpec.h"
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "parse.h"

using namespace Dyninst;
using namespace Dyninst::PatchAPI;

DEFINE_int32(stack_size, 24, "Stack size.");
DEFINE_int32(capture_at, 64,
             "Capture stack at the specified call stack depth.");

InstSpec is;

void InstrumentFunction(
    BPatch_function* function, const Parser& parser, PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns) {
  BPatch_Vector<BPatch_snippet*> args;
  BPatch_constExpr size((int32_t)FLAGS_stack_size);
  args.push_back(&size);
  BPatch_constExpr capture_at((int32_t)FLAGS_capture_at);
  args.push_back(&capture_at);

  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  // Entry instrumentation
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);

  BPatch_funcCallExpr entry(*(instrumentation_fns["entry"]), args);

  BPatchSnippetHandle* handle;
  handle = binary_edit->insertSnippet(entry, *entries, BPatch_callBefore,
                                      BPatch_lastSnippet, &is);
  DCHECK(handle != nullptr)
      << "Failed instrumenting nop entry instrumentation.";

  // Exit instrumentation
  std::vector<BPatch_point*>* exits = function->findPoint(BPatch_exit);
  if (function->getName() == "main") {
    // Print internal statistics related to stack and program exit
    BPatch_function* fn = instrumentation_fns["print_stats"];
    BPatch_funcCallExpr print_stats(*fn, args);

    handle = binary_edit->insertSnippet(print_stats, *exits, BPatch_callAfter,
                                        BPatch_lastSnippet, &is);
    DCHECK(handle != nullptr) << "Failed instrumenting statistics logging "
                                 "at main exit.";
  }

  if (exits == nullptr || exits->size() == 0) {
    fprintf(stderr, "Function %s does not have exits\n",
            function->getName().c_str());
    return;
  }

  BPatch_funcCallExpr exit(*(instrumentation_fns["exit"]), args);
  handle = nullptr;
  handle = binary_edit->insertSnippet(exit, *exits, BPatch_callAfter,
                                      BPatch_lastSnippet, &is);
  DCHECK(handle != nullptr)
      << "Failed instrumenting nop entry instrumentation.";
}

void Instrument(const Parser& parser, PatchMgr::Ptr patcher,
                std::map<std::string, BPatch_function*>& fns) {
  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  for (auto it = objects.begin(); it != objects.end(); it++) {
    BPatch_object* object = *it;

    // Only instrument main program
    if (IsSharedLibrary(object)) {
      continue;
    }

    std::vector<BPatch_module*> modules;
    object->modules(modules);

    for (auto it = modules.begin(); it != modules.end(); it++) {
      BPatch_module* module = *it;

      char funcname[2048];
      std::vector<BPatch_function*>* functions = module->getProcedures();

      for (auto it = functions->begin(); it != functions->end(); it++) {
        BPatch_function* function = *it;
        function->getName(funcname, 2048);

        ParseAPI::Function* f = ParseAPI::convert(function);
        // We should only instrument functions in .text
        ParseAPI::CodeRegion* codereg = f->region();
        ParseAPI::SymtabCodeRegion* symRegion =
            dynamic_cast<ParseAPI::SymtabCodeRegion*>(codereg);
        assert(symRegion);
        SymtabAPI::Region* symR = symRegion->symRegion();
        if (symR->getRegionName() != ".text")
          continue;

        InstrumentFunction(function, parser, patcher, fns);
      }
    }
  }
}

BPatch_function* FindFunctionByName(BPatch_image* image, std::string name) {
  BPatch_Vector<BPatch_function*> funcs;
  if (image->findFunction(name.c_str(), funcs,
                          /* showError */ true,
                          /* regex_case_sensitive */ true,
                          /* incUninstrumentable */ true) == nullptr ||
      !funcs.size() || funcs[0] == nullptr) {
    return nullptr;
  }
  return funcs[0];
}

void PopulateFunctions(BPatch_binaryEdit* binary_edit, const Parser& parser,
                       std::map<std::string, BPatch_function*>& fns) {
  DCHECK(binary_edit->loadLibrary("libdepth.so"))
      << "Failed to load tls library";

  std::string fn_prefix = "_litecfi_inc_depth";
  std::string key_prefix = "entry";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);

  fn_prefix = "_litecfi_sub_depth";
  key_prefix = "exit";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);

  fn_prefix = "_litecfi_print_stats";
  key_prefix = "print_stats";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);
}

void SetInstrumentationSpec() {
  is.saveRegs.push_back(Dyninst::x86_64::rax);
  is.saveRegs.push_back(Dyninst::x86_64::rdx);
  is.saveRegs.push_back(Dyninst::x86_64::rdi);
  is.saveRegs.push_back(Dyninst::x86_64::r11);
  is.saveRegs.push_back(Dyninst::x86_64::r10);

  is.trampGuard = false;
  is.redZone = false;
  is.raLoc = Dyninst::x86_64::r10;

  is.saveRegs.push_back(Dyninst::x86_64::rsi);
  is.saveRegs.push_back(Dyninst::x86_64::rcx);
  is.saveRegs.push_back(Dyninst::x86_64::r8);
  is.saveRegs.push_back(Dyninst::x86_64::r9);
}
int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);

  std::string usage("Usage : ./cfi <flags> binary");
  gflags::SetUsageMessage(usage);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);

  Parser parser = InitParser(binary);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  std::map<std::string, BPatch_function*> instrumentation_fns;

  PopulateFunctions(binary_edit, parser, instrumentation_fns);
  SetInstrumentationSpec();

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);

  Instrument(parser, patcher, instrumentation_fns);

  binary_edit->writeFile((binary + "_depth").c_str());
}
