
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

using namespace Dyninst::PatchAPI;

DECLARE_string(instrument);
DECLARE_bool(libs);
DECLARE_string(shadow_stack);
DECLARE_bool(vv);

static std::vector<bool> reserved;

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
  bool (*jit_fn_)(RegisterUsageInfo, AssemblerHolder&);

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

std::vector<bool> GetReservedAvxMask() {
  if (reserved.size() > 0) {
    return reserved;
  }

  int n_regs = 0;
  int reserved_from = 0;

  if (FLAGS_shadow_stack == "avx2") {
    n_regs = 16;
    reserved_from = 8;
  } else if (FLAGS_shadow_stack == "avx512") {
    n_regs = 32;
    reserved_from = 16;
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
  if (FLAGS_shadow_stack == "avx2") {
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

void SharedLibraryInstrumentation(
    BPatch_function* function, RegisterUsageInfo& info, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns) {
  BPatch_Vector<BPatch_snippet*> args;
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  // ---------- Instrument Function Entry ----------------

  // [1] Shadow stack push
  //
  // Shared library function call to shadow stack push at function entry
  BPatch_funcCallExpr stack_push(*(instrumentation_fns["push"]), args);
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);
  BPatchSnippetHandle* handle = binary_edit->insertSnippet(
      stack_push, *entries, BPatch_callBefore, BPatch_firstSnippet, nullptr);
  DCHECK(handle != nullptr) << "Failed instrumenting stack push.";

  // [2] Spill conflicting AVX registers used in both function and shadow stack
  //
  std::vector<uint8_t> collisions =
      GetRegisterCollisions(info.GetUnusedAvx2Mask());
  // Collisions in reverse order for restoring content back from the stack
  std::vector<uint8_t> reversed = collisions;
  std::reverse(reversed.begin(), reversed.end());

  if (collisions.size() > 0) {
    BPatch_funcCallExpr reg_spill = GetRegisterOperationSnippet(
        instrumentation_fns, collisions, "register_spill");

    handle = nullptr;
    handle = binary_edit->insertSnippet(reg_spill, *entries, BPatch_callBefore,
                                        BPatch_lastSnippet, nullptr);
    DCHECK(handle != nullptr) << "Failed instrumenting register spill.";
  }

  // ----------- Instrument Call Instructions -------------
  if (collisions.size() > 0) {
    std::vector<BPatch_point*>* calls = function->findPoint(BPatch_subroutine);

    if (calls->size() > 0) {
      // [1] Save the colliding register context
      //
      BPatch_funcCallExpr ctx_save = GetRegisterOperationSnippet(
          instrumentation_fns, collisions, "ctx_save");

      handle = nullptr;
      handle = binary_edit->insertSnippet(ctx_save, *calls, BPatch_callBefore,
                                          BPatch_firstSnippet);
      DCHECK(handle != nullptr) << "Failed instrumenting context save.";

      // [2] Restore the shadow stack state on AVX registers in reverse order
      //
      BPatch_funcCallExpr reg_peek = GetRegisterOperationSnippet(
          instrumentation_fns, reversed, "register_peek");

      handle = nullptr;
      handle = binary_edit->insertSnippet(reg_peek, *calls, BPatch_callBefore,
                                          BPatch_lastSnippet, nullptr);
      DCHECK(handle != nullptr) << "Failed instrumenting register restore.";

      // [3] Restore the register context in reverse order
      //
      BPatch_funcCallExpr ctx_restore = GetRegisterOperationSnippet(
          instrumentation_fns, reversed, "ctx_restore");

      handle = nullptr;
      handle = binary_edit->insertSnippet(ctx_restore, *calls, BPatch_callAfter,
                                          BPatch_firstSnippet, nullptr);

      DCHECK(handle != nullptr) << "Failed instrumenting context restore.";
    }
  }

  // ---------- Instrument Function Exit ----------------

  // [2] Shadow stack pop
  //
  // Shared library function call to shadow stack pop at function exit
  std::vector<BPatch_point*>* exits = function->findPoint(BPatch_exit);
  BPatch_funcCallExpr stack_pop(*(instrumentation_fns["pop"]), args);

  // Some functions (like exit()) do not feature function exits. Skip them.
  if (exits != nullptr && exits->size() > 0) {
    handle = binary_edit->insertSnippet(stack_pop, *exits);
    DCHECK(handle != nullptr) << "Failed instrumenting stack pop.";
  }

  // [1] Restore the spilled AVX shadow stack context in reverse order
  //
  if (collisions.size() > 0) {
    BPatch_funcCallExpr reg_restore = GetRegisterOperationSnippet(
        instrumentation_fns, reversed, "register_restore");

    handle = nullptr;
    if (exits != nullptr && exits->size() > 0) {
      /*
      handle = binary_edit->insertSnippet(
          reg_restore, *exits, BPatch_callBefore, BPatch_firstSnippet, nullptr);
          */
      handle = binary_edit->insertSnippet(reg_restore, *exits);
      DCHECK(handle != nullptr) << "Failed instrumenting register restore.";
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
    bool is_init_function) {
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
                                 instrumentation_fns);
    return;
  }
}

void InstrumentModule(
    BPatch_module* module, Code* const lib, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::set<std::string>& init_fns) {
  char funcname[2048];
  std::vector<BPatch_function*>* functions = module->getProcedures();

  for (auto it = functions->begin(); it != functions->end(); it++) {
    BPatch_function* function = *it;
    function->getName(funcname, 2048);

    std::string func(funcname);
    if (init_fns.find(func) != init_fns.end()) {
      InstrumentFunction(function, lib, parser, patcher, instrumentation_fns,
                         true);
    } else if (func == "pthread_create" || func == "main") {
      InstrumentTlsInit(function, parser, patcher, instrumentation_fns);
    } else {
      // Avoid instrumenting some internal libc functions
      if ((strcmp(funcname, "memset") != 0) &&
          (strcmp(funcname, "call_gmon_start") != 0) &&
          (strcmp(funcname, "frame_dummy") != 0) && funcname[0] != '_') {
        InstrumentFunction(function, lib, parser, patcher, instrumentation_fns,
                           false);
      }
    }
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

  std::vector<BPatch_module*> modules;
  object->modules(modules);

  for (auto it = modules.begin(); it != modules.end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    InstrumentModule(module, lib, parser, patcher, instrumentation_fns,
                     init_fns);
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

void PopulateRegisterContextOperations(
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

  fn_prefix = "litecfi_register_peek";
  key_prefix = "register_peek";
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

  fn_prefix = "litecfi_ctx_peek";
  key_prefix = "ctx_peek";
  for (int i = 1; i <= 8; i++) {
    std::string fn_name = fn_prefix + "_" + std::to_string(i);
    fns[key_prefix + "_" + std::to_string(i)] =
        FindFunctionByName(parser.image, fn_name);
  }

  std::string fn_name = "litecfi_mem_initialize";
  fns["tls_init"] = FindFunctionByName(parser.image, fn_name);
}

void Instrument(std::string binary, std::map<std::string, Code*>* const cache,
                const Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "\n\nInstrumentation Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "====================" << Endl;

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  std::map<std::string, BPatch_function*> instrumentation_fns;
  if (FLAGS_instrument == "shared") {
    RegisterUsageInfo info;

    // Mark reserved avx2 register as unused for the purpose of shadow stack
    // code generation
    if (FLAGS_shadow_stack == "avx2") {
      std::vector<bool>& mask =
          const_cast<std::vector<bool>&>(info.GetUnusedAvx2Mask());
      mask = GetReservedAvxMask();
    }

    std::string instrumentation_library =
        Codegen(const_cast<RegisterUsageInfo&>(info));

    std::cout << "\n\n" << instrumentation_library << "\n";

    DCHECK(binary_edit->loadLibrary(instrumentation_library.c_str()))
        << "Failed to load instrumentation library";

    /* Find code coverage functions in the instrumentation library */
    BPatch_function* stack_push_fn =
        FindFunctionByName(parser.image, kStackPushFunction);
    BPatch_function* stack_pop_fn =
        FindFunctionByName(parser.image, kStackPopFunction);

    instrumentation_fns["push"] = stack_push_fn;
    instrumentation_fns["pop"] = stack_pop_fn;
  }

  PopulateRegisterContextOperations(binary_edit, parser, instrumentation_fns);

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
      init_fns.insert("_start");
      init_fns.insert("__libc_csu_init");
      init_fns.insert("__libc_start_main");
    }

    InstrumentCodeObject(object, cache, parser, patcher, instrumentation_fns,
                         init_fns);
  }

  binary_edit->writeFile((binary + "_cfi").c_str());
}
