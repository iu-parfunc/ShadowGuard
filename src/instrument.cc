
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
#include "utils.h"

#include "Module.h"
#include "Symbol.h"

using namespace Dyninst;
using namespace Dyninst::PatchAPI;

DECLARE_string(instrument);
DECLARE_bool(libs);
DECLARE_string(shadow_stack);
DECLARE_bool(skip);
DECLARE_bool(stats);
DECLARE_bool(vv);
DECLARE_string(threat_model);
DECLARE_int32(reserved_from);

static std::vector<bool> reserved;

// Trampoline specification
static InstSpec is_init;
static InstSpec is_empty;
static InstSpec is[9];

void SetupInstrumentationSpec() {
  // Suppose we instrument a call to stack init at entry of A;
  // If A does not use r11, we dont need to save r11 (_start does not)
  is_init.trampGuard = false;
  is_init.redZone = false;

  is_empty.trampGuard = false;
  is_empty.redZone = false;
  is_empty.saveRegs.push_back(Dyninst::x86_64::r11);
  is_empty.saveRegs.push_back(Dyninst::x86_64::r10);

  // Setup the trampoline specification
  is[0].saveRegs.push_back(Dyninst::x86_64::rax);
  is[0].saveRegs.push_back(Dyninst::x86_64::rdx);
  is[0].saveRegs.push_back(Dyninst::x86_64::rdi);
  is[0].saveRegs.push_back(Dyninst::x86_64::r11);
  is[0].saveRegs.push_back(Dyninst::x86_64::r10);

  is[0].raLoc = Dyninst::x86_64::r10;
  is[0].trampGuard = false;
  is[0].redZone = false;

  is[1] = is[0];
  is[2] = is[1];
  is[2].saveRegs.push_back(Dyninst::x86_64::rsi);
  is[3] = is[2];
  is[3].saveRegs.push_back(Dyninst::x86_64::rdx);
  is[4] = is[3];
  is[4].saveRegs.push_back(Dyninst::x86_64::rcx);
  is[5] = is[4];
  is[5].saveRegs.push_back(Dyninst::x86_64::r8);
  is[6] = is[5];
  is[6].saveRegs.push_back(Dyninst::x86_64::r9);
  is[7] = is[6];
  is[8] = is[7];
}

class StackOpSnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit StackOpSnippet(RegisterUsageInfo info) : info_(info) {}

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    AssemblerHolder ah;
    jit_fn_(info_, ah);

    int size = ah.GetCode()->getCodeSize();
    char* temp_buf = (char*)malloc(size);

    size = ah.GetCode()->relocate(temp_buf);
    buf.copy(temp_buf, size);
    return true;
  }

 protected:
  std::string (*jit_fn_)(RegisterUsageInfo, AssemblerHolder&);

 private:
  RegisterUsageInfo info_;
};

class StackInitSnippet : public StackOpSnippet {
 public:
  explicit StackInitSnippet(RegisterUsageInfo info) : StackOpSnippet(info) {
    jit_fn_ = JitStackInit;
  }
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

class CallStackPushSnippet : public StackOpSnippet {
 public:
  explicit CallStackPushSnippet(RegisterUsageInfo info) : StackOpSnippet(info) {
    jit_fn_ = JitCallStackPush;
  }
};
class CallStackPushSnippet2 : public StackOpSnippet {
 public:
  explicit CallStackPushSnippet2(RegisterUsageInfo info)
      : StackOpSnippet(info) {
    jit_fn_ = JitCallStackPush2;
  }
};

class CallStackPopSnippet : public StackOpSnippet {
 public:
  explicit CallStackPopSnippet(RegisterUsageInfo info) : StackOpSnippet(info) {
    jit_fn_ = JitCallStackPop;
  }
};

class CallStackPopSnippet2 : public StackOpSnippet {
 public:
  explicit CallStackPopSnippet2(RegisterUsageInfo info) : StackOpSnippet(info) {
    jit_fn_ = JitCallStackPop2;
  }
};

std::vector<bool> GetReservedAvxMask() {
  if (reserved.size() > 0) {
    return reserved;
  }

  int n_regs = 0;
  int reserved_from = 0;

  if (FLAGS_shadow_stack == "avx_v2" || FLAGS_shadow_stack == "avx_v3") {
    n_regs = 16;
    reserved_from = FLAGS_reserved_from;
  } else if (FLAGS_shadow_stack == "avx512") {
    n_regs = 32;
    reserved_from = FLAGS_reserved_from;
  }

  for (int i = 0; i < n_regs; i++) {
    // We reserve extended avx2 registers for the stack
    if (i >= reserved_from) {
      reserved.push_back(true);
      continue;
    }
    reserved.push_back(false);
  }
  return reserved;
}

std::vector<uint8_t> GetRegisterCollisions(const std::vector<bool>& unused) {
  std::vector<bool> reserved = GetReservedAvxMask();

  int n_regs = 0;
  if (FLAGS_shadow_stack == "avx_v2" || FLAGS_shadow_stack == "avx_v3") {
    n_regs = 16;
  } else if (FLAGS_shadow_stack == "avx512") {
    n_regs = 32;
  }

  std::vector<uint8_t> collisions;
  for (int i = 0; i < n_regs; i++) {
    if (reserved[i] && !unused[i]) {
      collisions.push_back(i);
    }
  }
  return collisions;
}

BPatch_funcCallExpr GetRegisterOperationSnippet(
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::vector<uint8_t>& collisions, std::string op_prefix) {
  BPatch_function* fn = instrumentation_fns[op_prefix];
  DCHECK(fn) << "Couldn't find the register operation " << op_prefix;

  unsigned mask = 0;
  long n_regs = collisions.size();
  for (int i = 0; i < n_regs; i++) {
    uint8_t id = collisions[i];
    mask |= (1U << id);
  }

  BPatch_Vector<BPatch_snippet*> args;
  args.push_back(new BPatch_constExpr(mask));
  return BPatch_funcCallExpr(*fn, args);
}

void InsertInstrumentation(BPatch_function* function, Point::Type location,
                           Snippet::Ptr snippet, PatchMgr::Ptr patcher) {
  std::vector<Point*> points;
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)), location,
                      back_inserter(points));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* point = *it;
    point->pushBack(snippet);
  }
}

void SharedLibraryInstrumentation(
    BPatch_function* function, RegisterUsageInfo& info, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    bool isSystemCode) {
  BPatch_Vector<BPatch_snippet*> args;
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  BPatchSnippetHandle* handle;
  if (FLAGS_shadow_stack == "reloc") {
    function->relocateFunction();
    return;
  }
  if (FLAGS_shadow_stack == "savegpr") {
    // Noop instrumentation
    std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);
    BPatch_nullExpr snippet;
    handle = nullptr;
    handle = binary_edit->insertSnippet(snippet, *entries, BPatch_callBefore,
                                        BPatch_lastSnippet, &is[0]);
    DCHECK(handle != nullptr)
        << "Failed instrumenting nop entry instrumentation.";

    std::vector<BPatch_point*>* exits = function->findPoint(BPatch_exit);
    if (exits == nullptr || exits->size() == 0) {
      fprintf(stderr, "Function %s does not have exits\n",
              function->getName().c_str());
      return;
    }
    handle = nullptr;
    handle = binary_edit->insertSnippet(snippet, *exits, BPatch_callAfter,
                                        BPatch_lastSnippet, &is[0]);
    DCHECK(handle != nullptr)
        << "Failed instrumenting nop entry instrumentation.";
    return;
  }

  if (FLAGS_shadow_stack == "dispatch" || FLAGS_shadow_stack == "empty") {
    // Noop instrumentation
    std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);

    // BPatch_nullExpr snippet;
    BPatch_funcCallExpr nop_push(*(instrumentation_fns["push"]), args);
    handle = binary_edit->insertSnippet(nop_push, *entries, BPatch_callBefore,
                                        BPatch_lastSnippet, &is_init);
    DCHECK(handle != nullptr)
        << "Failed instrumenting nop entry instrumentation.";

    std::vector<BPatch_point*>* exits = function->findPoint(BPatch_exit);
    if (exits == nullptr || exits->size() == 0) {
      fprintf(stderr, "Function %s does not have exits\n",
              function->getName().c_str());
      return;
    }

    BPatch_funcCallExpr nop_pop(*(instrumentation_fns["pop"]), args);
    handle = binary_edit->insertSnippet(nop_pop, *exits, BPatch_callAfter,
                                        BPatch_lastSnippet, &is_init);
    DCHECK(handle != nullptr)
        << "Failed instrumenting nop entry instrumentation.";
    return;
  }
  function->relocateFunction();

  std::vector<uint8_t> collisions =
      GetRegisterCollisions(info.GetUnusedAvx2Mask());

  if (FLAGS_threat_model == "trust_system" && isSystemCode &&
      collisions.empty())
    return;

  InstSpec* isPtr;
  if (collisions.size() <= 0)
    isPtr = &is[collisions.size()];
  else
    isPtr = &is[1];

  // ---------- 1. Instrument Function Entry ----------------
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);

  // 1.a) Shadow stack push
  // Shared library function call to shadow stack push at function entry
  if (info.ShouldSkip()) {
    fprintf(
        stderr,
        "Skip function %s because it does not write to memory or write to SP\n",
        function->getName().c_str());
  }
  if (!info.ShouldSkip() &&
      (FLAGS_threat_model != "trust_system" || !isSystemCode)) {

    if (FLAGS_shadow_stack == "avx_v2" || FLAGS_shadow_stack == "avx_v3") {
      RegisterUsageInfo unused;
      std::vector<bool>& mask =
          const_cast<std::vector<bool>&>(unused.GetUnusedAvx2Mask());
      mask = GetReservedAvxMask();

      if (collisions.empty()) {
        BPatch_nullExpr snippet;
        handle = nullptr;
        handle = binary_edit->insertSnippet(
            snippet, *entries, BPatch_callBefore, BPatch_lastSnippet, &is_init);
        Snippet::Ptr call_stack_push =
            CallStackPushSnippet::create(new CallStackPushSnippet(unused));
        InsertInstrumentation(function, Point::FuncEntry, call_stack_push,
                              patcher);
      } else {
        Snippet::Ptr call_stack_push =
            CallStackPushSnippet2::create(new CallStackPushSnippet2(unused));
        InsertInstrumentation(function, Point::FuncEntry, call_stack_push,
                              patcher);
      }
    }
  }

  // 1.b) Save CFI context if necessary
  if (collisions.size() > 0) {
    BPatch_funcCallExpr reg_spill = GetRegisterOperationSnippet(
        instrumentation_fns, collisions, "register_spill");

    handle = nullptr;
    handle = binary_edit->insertSnippet(reg_spill, *entries, BPatch_callBefore,
                                        BPatch_lastSnippet, isPtr);
    DCHECK(handle != nullptr)
        << "Failed instrumenting saving CFI context at function entry.";
  }

  // 1.c) Restore user context if necessary
  if (collisions.size() > 0) {
    BPatch_funcCallExpr reg_spill = GetRegisterOperationSnippet(
        instrumentation_fns, collisions, "ctx_restore");

    handle = nullptr;
    handle = binary_edit->insertSnippet(reg_spill, *entries, BPatch_callBefore,
                                        BPatch_lastSnippet, isPtr);
    DCHECK(handle != nullptr)
        << "Failed instrumenting restoring user context at function entry.";
  }

  // ----------- 2. Instrument Call Instructions -------------
  if (collisions.size() > 0) {
    std::vector<BPatch_point*>* calls = function->findPoint(BPatch_subroutine);
    // Due to Dyninst's internal problem, instrumenting conditional tail calls
    // will cause semantic issues: the call instrumentation will be executed
    // regardless of whether the call happens or not.
    //
    // The problem is even worse because Dyninst treat tail calls as
    // non-returning, so post-call instrumentation is not installed for
    // conditional tail calls
    //
    // Here we are lucky because our pre-call instruction is a subset of our
    // exit instrumentation. And Dyninst handles conditional exit
    // instrumentation correctly. So, we work around this problem by skipping
    // all conditional call sites
    for (auto call_it = calls->begin(); call_it != calls->end();) {
      BPatch_basicBlock* b = (*call_it)->getBlock();
      std::vector<InstructionAPI::Instruction> insns;
      b->getInstructions(insns);
      Dyninst::InstructionAPI::Instruction insn = (*insns.rbegin());
      if (insn.getCategory() == InstructionAPI::c_BranchInsn &&
          insn.allowsFallThrough()) {
        // This is a conditional tail call, we skip it
        call_it = calls->erase(call_it);
      } else {
        ++call_it;
      }
    }

    if (calls->size() > 0) {
      // 2.a) Save user context before the call
      BPatch_funcCallExpr ctx_save = GetRegisterOperationSnippet(
          instrumentation_fns, collisions, "ctx_save");

      handle = nullptr;
      handle = binary_edit->insertSnippet(ctx_save, *calls, BPatch_callBefore,
                                          BPatch_lastSnippet);
      DCHECK(handle != nullptr)
          << "Failed instrumenting saving user context before call.";

      // 2.b) Restore CFI context before the call
      BPatch_funcCallExpr reg_spill = GetRegisterOperationSnippet(
          instrumentation_fns, collisions, "register_restore");

      handle = nullptr;
      handle = binary_edit->insertSnippet(reg_spill, *calls, BPatch_callBefore,
                                          BPatch_lastSnippet, isPtr);
      DCHECK(handle != nullptr)
          << "Failed instrumenting restoring CFI context at function entry.";

      // 2.c) Save CFI context after the call
      BPatch_funcCallExpr reg_save = GetRegisterOperationSnippet(
          instrumentation_fns, collisions, "register_spill");

      handle = nullptr;
      handle = binary_edit->insertSnippet(reg_save, *calls, BPatch_callAfter,
                                          BPatch_lastSnippet, isPtr);
      DCHECK(handle != nullptr) << "Failed instrumenting register restore.";

      // 2.d) Restore user context after the call
      //
      BPatch_funcCallExpr ctx_restore = GetRegisterOperationSnippet(
          instrumentation_fns, collisions, "ctx_restore");

      handle = nullptr;
      handle = binary_edit->insertSnippet(ctx_restore, *calls, BPatch_callAfter,
                                          BPatch_lastSnippet, isPtr);
      DCHECK(handle != nullptr)
          << "Failed instrumenting restoring user context after call.";
    }
  }

  // ---------- 3. Instrument Function Exit ----------------
  // Some functions (like exit()) do not feature function exits. Skip them.
  std::vector<BPatch_point*>* exits = function->findPoint(BPatch_exit);
  if (exits == nullptr || exits->size() == 0) {
    fprintf(stderr, "Function %s does not have exits\n",
            function->getName().c_str());
    return;
  }
  // 3.a) Save user context if necessary
  if (collisions.size() > 0) {
    BPatch_funcCallExpr ctx_save = GetRegisterOperationSnippet(
        instrumentation_fns, collisions, "ctx_save");

    handle = nullptr;
    handle = binary_edit->insertSnippet(ctx_save, *exits, BPatch_callAfter,
                                        BPatch_lastSnippet, isPtr);
    DCHECK(handle != nullptr)
        << "Failed instrumenting saving user context at function exit.";
  }

  // 3.b) Save restore CFI context if necessary
  if (collisions.size() > 0) {
    BPatch_funcCallExpr reg_restore = GetRegisterOperationSnippet(
        instrumentation_fns, collisions, "register_restore");

    handle = nullptr;
    handle = binary_edit->insertSnippet(reg_restore, *exits, BPatch_callAfter,
                                        BPatch_lastSnippet, isPtr);
    DCHECK(handle != nullptr)
        << "Failed instrumenting restoring cfi context at function exit.";
  }

  // 3.c) Shadow stack pop
  // Shared library function call to shadow stack pop at function exit
  if (!info.ShouldSkip() &&
      (FLAGS_threat_model != "trust_system" || !isSystemCode)) {

    if (FLAGS_shadow_stack == "avx_v2" || FLAGS_shadow_stack == "avx_v3") {
      if (FLAGS_stats && function->getName() == "main") {
        // Print internal statistics related to stack and program exit
        BPatch_funcCallExpr print_stats = GetRegisterOperationSnippet(
            instrumentation_fns, collisions, "print_stats");

        BPatch_Vector<BPatch_snippet*> args;
        handle = binary_edit->insertSnippet(
            print_stats, *exits, BPatch_callAfter, BPatch_lastSnippet, nullptr);
        DCHECK(handle != nullptr) << "Failed instrumenting statistics logging "
                                     "at main exit.";
      }

      RegisterUsageInfo unused;
      std::vector<bool>& mask =
          const_cast<std::vector<bool>&>(unused.GetUnusedAvx2Mask());
      mask = GetReservedAvxMask();
      if (collisions.empty()) {
        BPatch_nullExpr snippet;
        handle = nullptr;
        handle = binary_edit->insertSnippet(snippet, *exits, BPatch_callAfter,
                                            BPatch_lastSnippet, &is_init);
        Snippet::Ptr call_stack_push =
            CallStackPopSnippet::create(new CallStackPopSnippet(unused));
        InsertInstrumentation(function, Point::FuncExit, call_stack_push,
                              patcher);
      } else {
        Snippet::Ptr call_stack_push =
            CallStackPopSnippet2::create(new CallStackPopSnippet2(unused));
        InsertInstrumentation(function, Point::FuncExit, call_stack_push,
                              patcher);
      }
    }
  }
}

void InstrumentTlsInit(
    BPatch_function* function, const Parser& parser, PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns) {
  BPatch_Vector<BPatch_snippet*> args;
  BPatch_funcCallExpr tls_init(*(instrumentation_fns["tls_init"]), args);

  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);

  BPatchSnippetHandle* handle = binary_edit->insertSnippet(tls_init, *entries);
  DCHECK(handle != nullptr) << "Failed instrumenting tls initialization.";
}

void InstrumentFunction(
    BPatch_function* function, Code* lib, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    bool is_init_function, bool isSystemCode) {
  // Gets the function name in ParseAPI function instance
  std::string fn_name = Dyninst::PatchAPI::convert(function)->name();
  auto it = lib->register_usage.find(fn_name);
  DCHECK(it != lib->register_usage.end()) << "Could not find analysis "
                                          << " for function : " << fn_name;

  RegisterUsageInfo* info = it->second;

  if (FLAGS_vv) {
    StdOut(Color::YELLOW) << "     Function : " << fn_name << Endl;
  }

  // Instrument for initializing the stack in the init function
  if (is_init_function) {
    if (FLAGS_shadow_stack == "avx_v2" || FLAGS_shadow_stack == "avx_v3") {
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
    RegisterUsageInfo reserved;
    std::vector<bool>& mask =
        const_cast<std::vector<bool>&>(reserved.GetUnusedAvx2Mask());
    mask = GetReservedAvxMask();

    Snippet::Ptr stack_init =
        StackInitSnippet::create(new StackInitSnippet(reserved));
    InsertInstrumentation(function, Point::FuncEntry, stack_init, patcher);
    return;
  }

  SharedLibraryInstrumentation(function, *info, parser, patcher,
                               instrumentation_fns, isSystemCode);
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
    BPatch_module* module, Code* const lib, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::set<std::string>& init_fns, bool isSystemCode) {
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
                         true, isSystemCode);
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
                       false, isSystemCode);
  }
}

void InstrumentCodeObject(
    BPatch_object* object, std::map<std::string, Code*>* const cache,
    const Parser& parser, PatchMgr::Ptr patcher,
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

  DCHECK(lib) << "Couldn't find code object for : " << object->pathName();
  bool isSystemCode = IsSystemCode(object);
  std::vector<BPatch_module*> modules;
  object->modules(modules);

  for (auto it = modules.begin(); it != modules.end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    InstrumentModule(module, lib, parser, patcher, instrumentation_fns,
                     init_fns, isSystemCode);
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
    BPatch_binaryEdit* binary_edit, const Parser& parser,
    std::map<std::string, BPatch_function*>& fns) {
  DCHECK(binary_edit->loadLibrary("libtls.so")) << "Failed to load tls library";

  std::string fn_prefix = "litecfi_ctx_save";
  std::string key_prefix = "ctx_save";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);

  fn_prefix = "litecfi_ctx_restore";
  key_prefix = "ctx_restore";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);

  fn_prefix = "litecfi_register_spill";
  key_prefix = "register_spill";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);

  fn_prefix = "litecfi_register_restore";
  key_prefix = "register_restore";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);

  fn_prefix = "litecfi_stack_print_stats";
  key_prefix = "print_stats";
  fns[key_prefix] = FindFunctionByName(parser.image, fn_prefix);

  // std::string fn_name = "litecfi_mem_initialize";
  // fns["tls_init"] = FindFunctionByName(parser.image, fn_name);

  // In avx_v2 and up implementations, the stack push & pop related functions
  // are hidden symbols in libstack.so. We set up call pointers
  // in stack init. So, do not look for stack push & pop functions
  if (FLAGS_shadow_stack != "avx_v2" && FLAGS_shadow_stack != "avx_v3") {
    fns["push"] = FindFunctionByName(parser.image, kStackPushFunction);
    fns["pop"] = FindFunctionByName(parser.image, kStackPopFunction);
  } else {
    fns["stack_init"] =
        FindFunctionByName(parser.image, "litecfi_avx2_stack_init");
  }
}

void Instrument(std::string binary, std::map<std::string, Code*>* const cache,
                const Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "\n\nInstrumentation Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "====================" << Endl;

  StdOut(Color::BLUE) << "+ Instrumenting the binary..." << Endl;

  // Delete AVX2 register clearing instructions
  BPatch::bpatch->addDeleteInstructionOpcode(e_vzeroall);
  BPatch::bpatch->addDeleteInstructionOpcode(e_vzeroupper);

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  SetupInstrumentationSpec();

  std::map<std::string, BPatch_function*> instrumentation_fns;
  RegisterUsageInfo info;

  // Mark reserved avx2 registers as unused for the purpose of shadow stack
  // code generation
  if (FLAGS_shadow_stack == "avx_v2" || FLAGS_shadow_stack == "avx_v3") {
    std::vector<bool>& mask =
        const_cast<std::vector<bool>&>(info.GetUnusedAvx2Mask());
    mask = GetReservedAvxMask();
  }

  std::string instrumentation_library = "libstack.so";

  if (!FLAGS_skip) {
    instrumentation_library = Codegen(const_cast<RegisterUsageInfo&>(info));
  }

  if (FLAGS_shadow_stack != "reloc" && FLAGS_shadow_stack != "savegpr")
    DCHECK(binary_edit->loadLibrary(instrumentation_library.c_str()))
        << "Failed to load instrumentation library";

  if (FLAGS_shadow_stack != "reloc" && FLAGS_shadow_stack != "savegpr")
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

  binary_edit->writeFile((binary + "_cfi").c_str());
}
