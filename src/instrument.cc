
#include <algorithm>
#include <iostream>
#include <vector>

#include "BPatch.h"
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
DECLARE_bool(vv);
DECLARE_string(threat_model);
DECLARE_int32(reserved_from);

static std::vector<bool> reserved;

// Trampoline specification
static InstSpec is_init;
static InstSpec is[9];

void SetupInstrumentationSpec() {
  // Suppose we instrument a call to stack init at entry of A;
  // If A does not use r11, we dont need to save r11 (_start does not)
  is_init.trampGuard = false;
  is_init.redZone = false;
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
  explicit CallStackPushSnippet2(RegisterUsageInfo info) : StackOpSnippet(info) {
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

  if (FLAGS_shadow_stack == "avx2" || FLAGS_shadow_stack == "avx_v2") {
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
  if (FLAGS_shadow_stack == "avx2" || FLAGS_shadow_stack == "avx_v2") {
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
  long n_regs = collisions.size();
  DCHECK(n_regs <= 8)
      << "Can only save or restore up to 8 conflicting registers: " << n_regs;

  BPatch_function* fn =
      instrumentation_fns[op_prefix + "_" + std::to_string(n_regs)];
  DCHECK(fn) << "Couldn't find the register operation " << op_prefix << "_"
             << std::to_string(n_regs);

  BPatch_Vector<BPatch_snippet*> args;
  for (int i = 0; i < n_regs; i++) {
    BPatch_constExpr* reg = new BPatch_constExpr(collisions[i]);
    args.push_back(reg);
  }

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

    BPatch_funcCallExpr nop_push(*(instrumentation_fns["push"]), args);
    handle = binary_edit->insertSnippet(nop_push, *entries, BPatch_callBefore,
                                        BPatch_lastSnippet, &is[0]);
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
                                        BPatch_lastSnippet, &is[0]);
    DCHECK(handle != nullptr)
        << "Failed instrumenting nop entry instrumentation.";
    return;
  }

  std::vector<uint8_t> collisions =
      GetRegisterCollisions(info.GetUnusedAvx2Mask());

  if (FLAGS_threat_model == "trust_system" && isSystemCode && collisions.empty()) return;

  InstSpec* isPtr = &is[collisions.size()];

  // ---------- 1. Instrument Function Entry ----------------
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);

  // 1.a) Shadow stack push
  // Shared library function call to shadow stack push at function entry
  if (FLAGS_threat_model != "trust_system" || !isSystemCode) {

  if (FLAGS_shadow_stack == "avx2") {
    BPatch_funcCallExpr stack_push(*(instrumentation_fns["push"]), args);
    BPatch_funcCallExpr of_push(*(instrumentation_fns["overflow_push"]), args);

    // Check return value to determine whether stack is overflown
    BPatch_ifExpr push_of_check(
        BPatch_boolExpr(BPatch_eq, stack_push, BPatch_constExpr(0)), of_push);

    handle = nullptr;
    handle = binary_edit->insertSnippet(
        push_of_check, *entries, BPatch_callBefore, BPatch_lastSnippet, isPtr);
    DCHECK(handle != nullptr) << "Failed instrumenting stack push.";
  } else if (FLAGS_shadow_stack == "avx_v2") {
    RegisterUsageInfo unused;
    std::vector<bool>& mask =
        const_cast<std::vector<bool>&>(unused.GetUnusedAvx2Mask());
    mask = GetReservedAvxMask();

    if (collisions.empty()) {
      BPatch_nullExpr snippet;
      handle = nullptr;
      handle = binary_edit->insertSnippet(snippet, *entries, BPatch_callBefore,
                                          BPatch_lastSnippet, &is_init);
      Snippet::Ptr call_stack_push =
          CallStackPushSnippet::create(new CallStackPushSnippet(unused));
      InsertInstrumentation(function, Point::FuncEntry, call_stack_push, patcher);
    } else {
      Snippet::Ptr call_stack_push =
          CallStackPushSnippet2::create(new CallStackPushSnippet2(unused));
      InsertInstrumentation(function, Point::FuncEntry, call_stack_push, patcher);
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
  if (FLAGS_threat_model != "trust_system" || !isSystemCode) {

  if (FLAGS_shadow_stack == "avx2") {
    BPatch_funcCallExpr stack_pop(*(instrumentation_fns["pop"]), args);
    BPatch_funcCallExpr of_pop(*(instrumentation_fns["overflow_pop"]), args);

    // Check return value to determine whether stack is overflown
    BPatch_ifExpr pop_of_check(
        BPatch_boolExpr(BPatch_eq, stack_pop, BPatch_constExpr(0)), of_pop);

    handle = nullptr;
    handle = binary_edit->insertSnippet(pop_of_check, *exits, BPatch_callAfter,
                                        BPatch_lastSnippet, isPtr);
    DCHECK(handle != nullptr)
        << "Failed instrumenting stack pop at function exit.";
  } else if (FLAGS_shadow_stack == "avx_v2") {
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
      InsertInstrumentation(function, Point::FuncExit, call_stack_push, patcher);
    } else {
      Snippet::Ptr call_stack_push =
          CallStackPopSnippet2::create(new CallStackPopSnippet2(unused));
      InsertInstrumentation(function, Point::FuncExit, call_stack_push, patcher);

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

void InlinedInstrumentation(BPatch_function* function,
                            const RegisterUsageInfo& info, const Parser& parser,
                            PatchMgr::Ptr patcher) {
  // Inlined shadow stack push instrumentation at function entry
  Snippet::Ptr stack_push =
      StackPushSnippet::create(new StackPushSnippet(info));
  InsertInstrumentation(function, Point::FuncEntry, stack_push, patcher);

  // Spill conflicting registers

  // Instrument all calls to
  //   Save context precall
  //   Restore context after call

  // Inlined shadow stack pop instrumentation at function exit
  Snippet::Ptr stack_pop = StackPopSnippet::create(new StackPopSnippet(info));
  InsertInstrumentation(function, Point::FuncExit, stack_pop, patcher);

  // Restore spilled registers
}

void InstrumentFunction(
    BPatch_function* function, Code* lib, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    bool is_init_function,
    bool isSystemCode) {
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
    if (FLAGS_shadow_stack == "avx_v2") {
      BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
      BPatch_Vector<BPatch_snippet*> args;
      BPatch_funcCallExpr stack_init(*(instrumentation_fns["stack_init"]), args);
      std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);
      BPatchSnippetHandle* handle = nullptr;
      handle = binary_edit->insertSnippet(stack_init, *entries, BPatch_callBefore,
                                          BPatch_lastSnippet, &is_init);
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

  if (FLAGS_instrument == "inline") {
    InlinedInstrumentation(function, *info, parser, patcher);
    return;
  }

  if (FLAGS_instrument == "shared") {
    SharedLibraryInstrumentation(function, *info, parser, patcher,
                                 instrumentation_fns, isSystemCode);
    return;
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
    BPatch_module* module, Code* const lib, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::set<std::string>& init_fns,
    bool isSystemCode) {
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

  std::string fn_prefix = "litecfi_register_spill";
  std::string key_prefix = "register_spill";
  for (int i = 1; i <= 8; i++) {
    std::string fn_name = fn_prefix + "_" + std::to_string(i);
    fns[key_prefix + "_" + std::to_string(i)] =
        FindFunctionByName(parser.image, fn_name);
  }

  fn_prefix = "litecfi_register_restore";
  key_prefix = "register_restore";
  for (int i = 1; i <= 8; i++) {
    std::string fn_name = fn_prefix + "_" + std::to_string(i);
    fns[key_prefix + "_" + std::to_string(i)] =
        FindFunctionByName(parser.image, fn_name);
  }

  fn_prefix = "litecfi_ctx_save";
  key_prefix = "ctx_save";
  for (int i = 1; i <= 8; i++) {
    std::string fn_name = fn_prefix + "_" + std::to_string(i);
    fns[key_prefix + "_" + std::to_string(i)] =
        FindFunctionByName(parser.image, fn_name);
  }

  fn_prefix = "litecfi_ctx_restore";
  key_prefix = "ctx_restore";
  for (int i = 1; i <= 8; i++) {
    std::string fn_name = fn_prefix + "_" + std::to_string(i);
    fns[key_prefix + "_" + std::to_string(i)] =
        FindFunctionByName(parser.image, fn_name);
  }

  std::string fn_name = "litecfi_mem_initialize";
  fns["tls_init"] = FindFunctionByName(parser.image, fn_name);

  fn_name = "litecfi_overflow_stack_push";
  fns["overflow_push"] = FindFunctionByName(parser.image, fn_name);

  fn_name = "litecfi_overflow_stack_pop";
  fns["overflow_pop"] = FindFunctionByName(parser.image, fn_name);

  // In avx_v2 implementation, the stack push & pop functions 
  // are hidden symbols in libstack.so. We set up call pointers
  // in stack init. So, do not look for stack push & pop functions
  if (FLAGS_shadow_stack != "avx_v2") {
    fns["push"] = FindFunctionByName(parser.image, kStackPushFunction);
    fns["pop"] = FindFunctionByName(parser.image, kStackPopFunction);
  } else {
    fns["stack_init"] = FindFunctionByName(parser.image, "litecfi_avx2_stack_init");
  }
}

void Instrument(std::string binary, std::map<std::string, Code*>* const cache,
                const Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "\n\nInstrumentation Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "====================" << Endl;

  StdOut(Color::BLUE) << "+ Instrumenting the binary..." << Endl;

  // Delete AVX2 register clearing instructions
  BPatch::bpatch->addDeleteInstructionOpcode(e_vzeroall);
  //BPatch::bpatch->addDeleteInstructionOpcode(e_vzeroupper);

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  SetupInstrumentationSpec();

  std::map<std::string, BPatch_function*> instrumentation_fns;
  if (FLAGS_instrument == "shared") {
    RegisterUsageInfo info;

    // Mark reserved avx2 registers as unused for the purpose of shadow stack
    // code generation
    if (FLAGS_shadow_stack == "avx2" || FLAGS_shadow_stack == "avx_v2") {
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
  }
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
