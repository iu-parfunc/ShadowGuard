
#include <iostream>
#include <vector>

#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "BPatch_point.h"
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

void SharedLibraryInstrumentation(
    BPatch_function* function, const RegisterUsageInfo& info,
    const Parser& parser, PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    bool is_init_function) {
  BPatch_Vector<BPatch_snippet*> args;
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  // Shared library function call to shadow stack push at function entry
  BPatch_funcCallExpr stack_push(*(instrumentation_fns["push"]), args);
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);
  BPatchSnippetHandle* handle = binary_edit->insertSnippet(
      stack_push, *entries, BPatch_callBefore, BPatch_lastSnippet);
  DCHECK(handle != nullptr) << "Failed instrumenting function entry";

  // Shared library function call to shadow stack pop at function exit
  BPatch_funcCallExpr stack_pop(*(instrumentation_fns["pop"]), args);
  std::vector<BPatch_point*>* exits = function->findPoint(BPatch_entry);
  handle = binary_edit->insertSnippet(stack_pop, *exits, BPatch_callBefore,
                                      BPatch_lastSnippet);
  DCHECK(handle != nullptr) << "Failed instrumenting function exit";
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
                            PatchMgr::Ptr patcher, bool is_init_function) {
  // Inlined shadow stack push instrumentation at function entry
  Snippet::Ptr stack_push =
      StackPushSnippet::create(new StackPushSnippet(info));
  InsertInstrumentation(function, Point::FuncEntry, stack_push, patcher);

  // Inlined shadow stack pop instrumentation at function exit
  Snippet::Ptr stack_pop = StackPopSnippet::create(new StackPopSnippet(info));
  InsertInstrumentation(function, Point::FuncExit, stack_pop, patcher);
}

void InstrumentFunction(
    BPatch_function* function, const RegisterUsageInfo& info,
    const Parser& parser, PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    bool is_init_function) {
  // Instrument for initializing the stack in the init function
  if (is_init_function) {
    Snippet::Ptr stack_init =
        StackInitSnippet::create(new StackInitSnippet(info));
    InsertInstrumentation(function, Point::FuncEntry, stack_init, patcher);
    return;
  }

  if (FLAGS_instrument == "inline") {
    InlinedInstrumentation(function, info, parser, patcher, is_init_function);
    return;
  }

  if (FLAGS_instrument == "shared") {
    SharedLibraryInstrumentation(function, info, parser, patcher,
                                 instrumentation_fns, is_init_function);
    return;
  }
}

bool IsLibC(BPatch_object* object) {
  if (object->pathName().find("libc") != std::string::npos) {
    return true;
  }
  return false;
}

void InstrumentModule(
    BPatch_module* module, const RegisterUsageInfo& info, const Parser& parser,
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
      InstrumentFunction(function, info, parser, patcher, instrumentation_fns,
                         true);
    } else {
      // Avoid instrumenting some internal libc functions
      if ((strcmp(funcname, "memset") != 0) &&
          (strcmp(funcname, "call_gmon_start") != 0) &&
          (strcmp(funcname, "frame_dummy") != 0) && funcname[0] != '_') {
        InstrumentFunction(function, info, parser, patcher, instrumentation_fns,
                           false);
      }
    }
  }
}

void InstrumentCodeObject(
    BPatch_object* object, const RegisterUsageInfo& info, const Parser& parser,
    PatchMgr::Ptr patcher,
    std::map<std::string, BPatch_function*>& instrumentation_fns,
    const std::set<std::string>& init_fns) {
  std::vector<BPatch_module*> modules;
  object->modules(modules);

  std::string file_name = GetFileNameFromPath(object->pathName());

  for (auto it = modules.begin(); it != modules.end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    InstrumentModule(module, info, parser, patcher, instrumentation_fns,
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

void Instrument(std::string binary, const RegisterUsageInfo& info,
                const Parser& parser) {
  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  std::map<std::string, BPatch_function*> instrumentation_fns;
  if (FLAGS_instrument == "shared") {
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

    printf(" %p %p\n", stack_push_fn, stack_pop_fn);

    instrumentation_fns["push"] = stack_push_fn;
    instrumentation_fns["pop"] = stack_pop_fn;
  }

  for (auto it = objects.begin(); it != objects.end(); it++) {
    BPatch_object* object = *it;

    // Skip other shared libraries for now
    if (!FLAGS_libs && IsSharedLibrary(object)) {
      continue;
    }

    // This is the main program text. Mark some functions as premain
    // initialization functions. These function entries will be instrumentated
    // for stack initialization.
    std::set<std::string> init_fns;
    init_fns.insert("_start");
    init_fns.insert("__libc_csu_init");
    init_fns.insert("__libc_start_main");
    InstrumentCodeObject(object, info, parser, patcher, instrumentation_fns,
                         init_fns);
  }

  binary_edit->writeFile((binary + "_cfi").c_str());
}
