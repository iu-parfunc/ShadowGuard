
#include <algorithm>
#include <iostream>
#include <vector>

#include "BPatch.h"
#include "BPatch_basicBlock.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "BPatch_point.h"
#include "InstSpec.h"
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"
#include "asmjit/asmjit.h"
#include "codegen.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "jit.h"
#include "parse.h"
#include "pass_manager.h"
#include "passes.h"
#include "utils.h"

#include "Module.h"
#include "Symbol.h"

using namespace Dyninst;
using namespace Dyninst::PatchAPI;

DECLARE_string(instrument);
DECLARE_bool(libs);
DECLARE_string(shadow_stack);
DECLARE_string(shadow_stack_protection);
DECLARE_string(output);
DECLARE_bool(skip);
DECLARE_bool(stats);
DECLARE_bool(vv);
DECLARE_string(threat_model);

// Trampoline specification
static InstSpec is_init;
static InstSpec is_empty;

void SetupInstrumentationSpec() {
  // Suppose we instrument a call to stack init at entry of A;
  // If A does not use r11, we dont need to save r11 (_start does not)
  is_init.trampGuard = false;
  is_init.redZone = false;
  is_init.saveRegs.push_back(x86_64::rax);
  is_init.saveRegs.push_back(x86_64::rdx);

  is_empty.trampGuard = false;
  is_empty.redZone = false;
}

class StackOpSnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit StackOpSnippet(RegisterUsageInfo info) : info_(info) {}

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    AssemblerHolder ah;
    jit_fn_(info_, ah);

    size_t size = ah.GetCode()->codeSize();
    char* temp_buf = (char*)malloc(size);

    ah.GetCode()->relocateToBase((uint64_t)temp_buf);

    size = ah.GetCode()->codeSize();
    ah.GetCode()->copyFlattenedData(temp_buf, size,
                                    asmjit::CodeHolder::kCopyWithPadding);

    buf.copy(temp_buf, size);
    return true;
  }

 protected:
  std::string (*jit_fn_)(RegisterUsageInfo, AssemblerHolder&);

 private:
  RegisterUsageInfo info_;
};

class StackPushSnippet : public StackOpSnippet {
 public:
  explicit StackPushSnippet(RegisterUsageInfo info) : StackOpSnippet(info) {
    jit_fn_ = JitStackPush;
  }
};

class StackPopSnippet : public StackOpSnippet {
 public:
  explicit StackPopSnippet(RegisterUsageInfo info) : StackOpSnippet(info) {
    jit_fn_ = JitStackPop;
  }
};

void InsertSnippet(BPatch_function* function, Point::Type location,
                   Snippet::Ptr snippet, PatchMgr::Ptr patcher) {
  std::vector<Point*> points;
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)), location,
                      back_inserter(points));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* point = *it;
    point->pushBack(snippet);
  }
}

void InsertInstrumentation(
    BPatch_function* function, RegisterUsageInfo* info,
    const litecfi::Parser& parser, PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    bool isSystemCode) {

  if (FLAGS_shadow_stack == "mem") {
    if (FLAGS_threat_model == "trust_system" && isSystemCode) {
      return;
    }

    if (FLAGS_vv) {
      StdOut(Color::YELLOW)
          << "     Function : " << Dyninst::PatchAPI::convert(function)->name()
          << Endl;
    }

    RegisterUsageInfo dummy;
    Snippet::Ptr stack_push =
        StackPushSnippet::create(new StackPushSnippet(dummy));
    InsertSnippet(function, Point::FuncEntry, stack_push, patcher);

    Snippet::Ptr stack_pop =
        StackPopSnippet::create(new StackPopSnippet(dummy));
    InsertSnippet(function, Point::FuncExit, stack_pop, patcher);

    BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
    BPatch_nullExpr nopSnippet;
    vector<BPatch_point*> points;
    function->getEntryPoints(points);
    binary_edit->insertSnippet(nopSnippet, points, BPatch_callBefore,
                               BPatch_lastSnippet, &is_empty);

    points.clear();
    function->getExitPoints(points);
    binary_edit->insertSnippet(nopSnippet, points, BPatch_callAfter,
                               BPatch_lastSnippet, &is_empty);

    return;
  }
}

void InstrumentFunction(
    BPatch_function* function, Code* lib, const litecfi::Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::map<uint64_t, Function*>& skippable, bool is_init_function,
    bool isSystemCode) {

  // REMOVE(chamibuddhika)
  // auto it = lib->register_usage.find(fn_name);
  // DCHECK(it != lib->register_usage.end()) << "Could not find analysis "
  // RegisterUsageInfo* info = it->second;

  RegisterUsageInfo* info = nullptr;

  // Instrument for initializing the stack in the init function
  if (is_init_function) {
    if (FLAGS_shadow_stack == "mem") {
      BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
      BPatch_Vector<BPatch_snippet*> args;
      BPatch_funcCallExpr stack_init(*(instrumentation_fns["stack_init"]),
                                     args);
      std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);
      BPatchSnippetHandle* handle = nullptr;
      handle =
          binary_edit->insertSnippet(stack_init, *entries, BPatch_callBefore,
                                     BPatch_lastSnippet, &is_init);
      DCHECK(handle != nullptr)
          << "Failed to instrument init function for stack initialization.";

      return;
    }
  }

  if (skippable.find(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
          function->getBaseAddr()))) == skippable.end()) {
    InsertInstrumentation(function, info, parser, patcher, instrumentation_fns,
                          isSystemCode);
  } else {
    StdOut(Color::YELLOW, FLAGS_vv)
        << "Skipping function : " << function->getName() << Endl;
  }
}

static void GetIFUNCs(BPatch_module* module,
                      std::set<Dyninst::Address>& addrs) {
  SymtabAPI::Module* sym_mod = SymtabAPI::convert(module);
  std::vector<SymtabAPI::Symbol*> ifuncs;

  // Dyninst represents IFUNC as ST_INDIRECT
  sym_mod->getAllSymbolsByType(ifuncs, SymtabAPI::Symbol::ST_INDIRECT);
  for (auto sit = ifuncs.begin(); sit != ifuncs.end(); ++sit) {
    addrs.insert((Address)(*sit)->getOffset());
  }
}

void InstrumentModule(
    BPatch_module* module, Code* const lib, const litecfi::Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::set<std::string>& init_fns,
    const std::map<uint64_t, Function*>& skippable, bool isSystemCode) {
  char funcname[2048];
  std::vector<BPatch_function*>* functions = module->getProcedures();

  std::set<Dyninst::Address> ifuncAddrs;
  GetIFUNCs(module, ifuncAddrs);

  for (auto it = functions->begin(); it != functions->end(); it++) {
    BPatch_function* function = *it;
    function->getName(funcname, 2048);

    std::string func(funcname);
    if (init_fns.find(func) != init_fns.end()) {
      InstrumentFunction(function, lib, parser, patcher, instrumentation_fns,
                         skippable, true, isSystemCode);
      continue;
    }

    ParseAPI::Function* f = ParseAPI::convert(function);
    if (f->retstatus() == ParseAPI::NORETURN)
      continue;

    // We should only instrument functions in .text
    ParseAPI::CodeRegion* codereg = f->region();
    ParseAPI::SymtabCodeRegion* symRegion =
        dynamic_cast<ParseAPI::SymtabCodeRegion*>(codereg);
    assert(symRegion);
    SymtabAPI::Region* symR = symRegion->symRegion();
    if (symR->getRegionName() != ".text")
      continue;

    InstrumentFunction(function, lib, parser, patcher, instrumentation_fns,
                       skippable, false, isSystemCode);
  }
}

void InstrumentCodeObject(
    BPatch_object* object, std::map<std::string, Code*>* const cache,
    const litecfi::Parser& parser, PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::set<std::string>& init_fns) {
  if (!IsSharedLibrary(object)) {
    StdOut(Color::GREEN, FLAGS_vv) << "\n  >> Instrumenting main application "
                                   << object->pathName() << Endl;
  } else {
    StdOut(Color::GREEN, FLAGS_vv)
        << "\n    Instrumenting " << object->pathName() << Endl;
  }

  Code* lib = nullptr;
  auto it = cache->find(object->pathName());
  if (it != cache->end()) {
    lib = it->second;
  }

  // REMOVE(chamibuddhika)
  // DCHECK(lib) << "Couldn't find code object for : " << object->pathName(); //

  bool isSystemCode = IsSystemCode(object);
  std::vector<BPatch_module*> modules;
  object->modules(modules);

  // Do the static analysis on this code and obtain skippable functions.
  CodeObject* co = Dyninst::ParseAPI::convert(object);
  co->parse();

  PassManager* pm = new PassManager;
  pm->AddPass(new LeafAnalysisPass())
      ->AddPass(new StackAnalysisPass())
      ->AddPass(new NonLeafSafeWritesPass());
  std::set<Function*> safe = pm->Run(co);
  std::map<uint64_t, Function*> skippable;
  for (auto f : safe) {
    skippable[f->addr()] = f;
  }

  for (auto it = modules.begin(); it != modules.end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    InstrumentModule(module, lib, parser, patcher, instrumentation_fns,
                     init_fns, skippable, isSystemCode);
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

void PopulateRegisterStackOperations(
    BPatch_binaryEdit* binary_edit, const litecfi::Parser& parser,
    std::map<std::string, BPatch_function*>& fns) {
  fns["stack_init"] =
      FindFunctionByName(parser.image, "litecfi_init_mem_region");
  return;
}

void Instrument(std::string binary, std::map<std::string, Code*>* const cache,
                const litecfi::Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "\n\nInstrumentation Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "====================" << Endl;

  StdOut(Color::BLUE) << "+ Instrumenting the binary..." << Endl;

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  SetupInstrumentationSpec();

  std::map<std::string, BPatch_function*> instrumentation_fns;
  RegisterUsageInfo info;

  std::string instrumentation_library = "libstack.so";

  if (!FLAGS_skip) {
    instrumentation_library = Codegen(const_cast<RegisterUsageInfo&>(info));
  }

  DCHECK(binary_edit->loadLibrary(instrumentation_library.c_str()))
      << "Failed to load instrumentation library";
  PopulateRegisterStackOperations(binary_edit, parser, instrumentation_fns);

  for (auto it = objects.begin(); it != objects.end(); it++) {
    BPatch_object* object = *it;

    // Skip other shared libraries for now
    if (!FLAGS_libs && IsSharedLibrary(object)) {
      continue;
    }

    std::set<std::string> init_fns;
    if (!IsSharedLibrary(object)) {
      // This is the main program text. Mark some functions as premain
      // initialization functions. These function entries will be instrumentated
      // for stack initialization.
      // init_fns.insert("__libc_init_first");
      init_fns.insert("_start");
      // init_fns.insert("__libc_csu_init");
      // init_fns.insert("__libc_start_main");
    }

    InstrumentCodeObject(object, cache, parser, patcher, instrumentation_fns,
                         init_fns);
  }

  if (FLAGS_output.empty()) {
    binary_edit->writeFile((binary + "_cfi").c_str());
  } else {
    binary_edit->writeFile(FLAGS_output.c_str());
  }
}
