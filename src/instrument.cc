
#include <vector>

#include "BPatch.h"
#include "BPatch_function.h"
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"
#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "jit.h"
#include "utils.h"

using namespace Dyninst::PatchAPI;

DECLARE_bool(vv);

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

class StackPushSnippet : public StackOpSnippet {
 public:
  explicit StackPushSnippet(const RegisterUsageInfo& info)
      : StackOpSnippet(info) {
    StdOut(Color::BLUE, FLAGS_vv) << "\nCode Generation " << Endl;
    StdOut(Color::BLUE, FLAGS_vv) << "=================" << Endl;

    StdOut(Color::BLUE)
        << "\n[Code Generation] Setting up stack push code generation." << Endl;

    jit_fn_ = JitStackPush;
  }
};

class StackPopSnippet : public StackOpSnippet {
 public:
  explicit StackPopSnippet(const RegisterUsageInfo& info)
      : StackOpSnippet(info) {
    StdOut(Color::BLUE)
        << "[Code Generation] Setting up stack pop code generation." << Endl;

    jit_fn_ = JitStackPop;
  }
};

void InstrumentFunction(BPatch_function* function, PatchMgr::Ptr patcher,
                        Snippet::Ptr& push, Snippet::Ptr& pop,
                        const RegisterUsageInfo& info) {
  // Shadow stack push instrumentation at function entry
  std::vector<Point*> points;
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)),
                      Point::FuncEntry, back_inserter(points));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* point = *it;
    point->pushBack(push);
  }

  points.clear();

  // Shadow stack pop and test instrumentation at function exit
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)),
                      Point::FuncExit, back_inserter(points));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* pt = *it;
    pt->pushBack(pop);
  }
}

void InstrumentModule(BPatch_module* module, PatchMgr::Ptr patcher,
                      Snippet::Ptr& push, Snippet::Ptr& pop,
                      const RegisterUsageInfo& info) {
  char funcname[2048];
  std::vector<BPatch_function*>* functions = module->getProcedures();

  for (auto it = functions->begin(); it != functions->end(); it++) {
    BPatch_function* function = *it;
    function->getName(funcname, 2048);

    // CRITERIA FOR INSTRUMENTATION:
    // don't handle:
    //   - memset() or call_gmon_start() or frame_dummy()
    //   - functions that begin with an underscore
    if ((strcmp(funcname, "memset") != 0) &&
        (strcmp(funcname, "call_gmon_start") != 0) &&
        (strcmp(funcname, "frame_dummy") != 0) && funcname[0] != '_') {
      // printf("Function : %s\n", funcname);
      InstrumentFunction(function, patcher, push, pop, info);
    }
  }
}

void InstrumentApplication(BPatch_addressSpace* app,
                           const RegisterUsageInfo& info) {
  BPatch_image* image = app->getImage();
  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(app);

  Snippet::Ptr push = StackPushSnippet::create(new StackPushSnippet(info));
  Snippet::Ptr pop = StackPopSnippet::create(new StackPopSnippet(info));

  std::vector<BPatch_module*>* modules = image->getModules();
  for (auto it = modules->begin(); it != modules->end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    // for the purposes of this test,
    // don't handle our own library or libm
    if (strcmp(modname, "libdyntest.so") == 0 ||
        strcmp(modname, "libm.so.6") == 0 ||
        strcmp(modname, "libc.so.6") == 0) {
      continue;
    }

    /*
    if (module->isSharedLib()) {
      // printf("\nSkipping Module : %s\n\n", modname);
      continue;
    }
    */

    // printf("\nModule : %s\n\n", modname);
    InstrumentModule(module, patcher, push, pop, info);
  }
}

void Instrument(std::string binary, const RegisterUsageInfo& info) {
  // initalize DynInst library
  BPatch* bpatch = new BPatch;

  StdOut(Color::BLUE, FLAGS_vv) << "\n\nInstrumentation Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "====================" << Endl;

  StdOut(Color::BLUE) << "\n[Instrumentation] Instrumenting the binary ..."
                      << Endl;

  // open binary and linked shared libraries
  BPatch_addressSpace* app = bpatch->openBinary(binary.c_str(), true);

  if (app == NULL) {
    printf("ERROR: Unable to open application.\n");
    exit(EXIT_FAILURE);
  }

  InstrumentApplication(app, info);
  ((BPatch_binaryEdit*)app)->writeFile((binary + "_cfi").c_str());

  StdOut(Color::BLUE) << "\n[Instrumentation] Instrumentation complete."
                      << Endl;
}
