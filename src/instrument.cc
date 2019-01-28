
#include <vector>

#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"
#include "asmjit/asmjit.h"
#include "jit.h"
#include "parse.h"
#include "utils.h"

using namespace Dyninst::PatchAPI;

// Error handler that just prints the error and lets AsmJit ignore it.
class PrintErrorHandler : public asmjit::ErrorHandler {
 public:
  // Return `true` to set last error to `err`, return `false` to do nothing.
  bool handleError(asmjit::Error err, const char* message,
                   asmjit::CodeEmitter* origin) override {
    fprintf(stderr, "ERROR: %s\n", message);
    return false;
  }
};

class StackOpSnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit StackOpSnippet(const RegisterUsageInfo& info) : info_(info) {}

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    asmjit::JitRuntime rt;
    PrintErrorHandler eh;

    asmjit::CodeHolder code;
    code.init(rt.getCodeInfo());
    code.setErrorHandler(&eh);

    asmjit::X86Assembler a(&code);

    jit_fn_(info_, &a);

    int size = code.getCodeSize();
    char* temp_buf = (char*)malloc(size);

    size = code.relocate(temp_buf);
    buf.copy(temp_buf, size);
    return true;
  }

 protected:
  bool (*jit_fn_)(RegisterUsageInfo, asmjit::X86Assembler*);

 private:
  const RegisterUsageInfo& info_;
};

class StackInitSnippet : public StackOpSnippet {
 public:
  explicit StackInitSnippet(const RegisterUsageInfo& info)
      : StackOpSnippet(info) {
    jit_fn_ = JitStackInit;
  }
};

class StackPushSnippet : public StackOpSnippet {
 public:
  explicit StackPushSnippet(const RegisterUsageInfo& info)
      : StackOpSnippet(info) {
    jit_fn_ = JitStackPush;
  }
};

class StackPopSnippet : public StackOpSnippet {
 public:
  explicit StackPopSnippet(const RegisterUsageInfo& info)
      : StackOpSnippet(info) {
    jit_fn_ = JitStackPop;
  }
};

void InstrumentFunction(BPatch_function* function, PatchMgr::Ptr patcher,
                        const RegisterUsageInfo& info, bool is_init_function) {
  // Instrument for initializing the stack in the init function
  if (is_init_function) {
    std::vector<Point*> points;
    patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)),
                        Point::FuncEntry, back_inserter(points));

    Snippet::Ptr snippet = StackInitSnippet::create(new StackInitSnippet(info));

    for (auto it = points.begin(); it != points.end(); ++it) {
      Point* pt = *it;
      pt->pushBack(snippet);
    }
    return;
  }

  // Shadow stack push instrumentation at function entry
  std::vector<Point*> points;
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)),
                      Point::FuncEntry, back_inserter(points));

  Snippet::Ptr snippet = StackPushSnippet::create(new StackPushSnippet(info));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* point = *it;
    point->pushBack(snippet);
  }

  points.clear();

  // Shadow stack pop and test instrumentation at function exit
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)),
                      Point::FuncExit, back_inserter(points));

  snippet = StackPopSnippet::create(new StackPopSnippet(info));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* pt = *it;
    pt->pushBack(snippet);
  }
}

bool IsLibC(BPatch_object* object) {
  if (object->pathName().find("libc") != std::string::npos) {
    return true;
  }
  return false;
}

void InstrumentModule(BPatch_module* module, PatchMgr::Ptr patcher,
                      const RegisterUsageInfo& info,
                      std::string function_filter = "") {
  char funcname[2048];
  std::vector<BPatch_function*>* functions = module->getProcedures();

  std::vector<std::string> filter_functions = Split(function_filter, ',');
  for (auto it = functions->begin(); it != functions->end(); it++) {
    BPatch_function* function = *it;
    function->getName(funcname, 2048);

    std::string func(funcname);
    for (auto const& filter_function : filter_functions) {
      if (filter_function == func) {
        // Only instrument init function from libc for now
        InstrumentFunction(function, patcher, info, true);
        return;
      }
    }

    // CRITERIA FOR INSTRUMENTATION:
    // don't handle:
    //   - memset() or call_gmon_start() or frame_dummy()
    //   - functions that begin with an underscore
    if ((strcmp(funcname, "memset") != 0) &&
        (strcmp(funcname, "call_gmon_start") != 0) &&
        (strcmp(funcname, "frame_dummy") != 0) && funcname[0] != '_') {
      InstrumentFunction(function, patcher, info, false);
    }
  }
}

void InstrumentCodeObject(BPatch_object* object, const RegisterUsageInfo& info,
                          const Parser& parser,
                          std::string function_filter = "") {
  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);

  std::vector<BPatch_module*> modules;
  object->modules(modules);

  std::string file_name = GetFileNameFromPath(object->pathName());

  for (auto it = modules.begin(); it != modules.end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    InstrumentModule(module, patcher, info);
  }
}

void Instrument(std::string binary, const RegisterUsageInfo& info,
                const Parser& parser) {
  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  for (auto it = objects.begin(); it != objects.end(); it++) {
    BPatch_object* object = *it;

    if (IsLibC(object)) {
      // Instrument just the init functions in libc
      InstrumentCodeObject(object, info, parser,
                           "__libc_csu_init, __libc_start_main");
    }

    // Skip other shared libraries for now
    if (IsSharedLibrary(object)) {
      continue;
    }

    InstrumentCodeObject(object, info, parser);
  }

  ((BPatch_binaryEdit*)parser.app)->writeFile((binary + "_cfi").c_str());
}
