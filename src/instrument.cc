
#include <algorithm>
#include <iostream>
#include <vector>

#include "BPatch.h"
#include "BPatch_basicBlock.h"
#include "BPatch_edge.h"
#include "BPatch_flowGraph.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "BPatch_point.h"

#include "InstSpec.h"
#include "PatchMgr.h"
#include "Point.h"
#include "Snippet.h"
#include "asmjit/asmjit.h"
#include "assembler.h"
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

DECLARE_bool(libs);
DECLARE_bool(vv);

DECLARE_string(output);
DECLARE_string(shadow_stack);
DECLARE_string(threat_model);
DECLARE_string(stats);
DECLARE_string(skip_list);

// Thread local shadow stack initialization function name.
static constexpr char kShadowStackInitFn[] = "litecfi_init_mem_region";
// Init function which needs to be instrumented with a call the thread local
// shadow stack initialzation function above.
static constexpr char kInitFn[] = "_start";

// Trampoline specifications.
static InstSpec is_init;
static InstSpec is_empty;
static std::set<Address> skip_addrs;
static int fastPathFunction = 0;

class StackOpSnippet : public Dyninst::PatchAPI::Snippet {
 public:
  explicit StackOpSnippet(FuncSummary* summary) : summary_(summary) {}

  bool generate(Dyninst::PatchAPI::Point* pt, Dyninst::Buffer& buf) override {
    AssemblerHolder ah;
    jit_fn_(pt, summary_, ah);

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
  std::string (*jit_fn_)(Dyninst::PatchAPI::Point* pt, FuncSummary* summary,
                         AssemblerHolder&);

 private:
  FuncSummary* summary_;
};

class StackPushSnippet : public StackOpSnippet {
 public:
  explicit StackPushSnippet(FuncSummary* summary) : StackOpSnippet(summary) {
    jit_fn_ = JitStackPush;
  }
};

class StackPopSnippet : public StackOpSnippet {
 public:
  explicit StackPopSnippet(FuncSummary* summary) : StackOpSnippet(summary) {
    jit_fn_ = JitStackPop;
  }
};

bool IsNonreturningCall(Point* point) {
  PatchBlock* exitBlock = point->block();
  assert(exitBlock);
  bool call = false;
  bool callFT = false;
  for (auto e : exitBlock->targets()) {
    if (e->type() == CALL)
      call = true;
    if (e->type() == CALL_FT)
      callFT = true;
  }
  return (call && !callFT);
}

void InsertSnippet(BPatch_function* function, Point::Type location,
                   Snippet::Ptr snippet, PatchMgr::Ptr patcher) {
  std::vector<Point*> points;
  patcher->findPoints(Scope(Dyninst::PatchAPI::convert(function)), location,
                      back_inserter(points));

  for (auto it = points.begin(); it != points.end(); ++it) {
    Point* point = *it;
    // Do not instrument function exits that are calls to non-returning
    // functions because the stack frame may still not be tear down, causing the
    // instrumentation to get wrong return address.
    if (location == Point::FuncExit && IsNonreturningCall(point))
      continue;
    point->pushBack(snippet);
  }
}

bool CheckFastPathFunction(BPatch_basicBlock*& entry,
                           vector<BPatch_basicBlock*>& exits,
                           BPatch_function* f) {
  BPatch_flowGraph* cfg = f->getCFG();

  // Should have only one function entry block.
  std::vector<BPatch_basicBlock*> eb;
  cfg->getEntryBasicBlock(eb);
  if (eb.size() != 1)
    return false;
  BPatch_basicBlock* func_entry = eb[0];

  // The function entry block should not contain memory writes.
  std::vector<Dyninst::InstructionAPI::Instruction> insns;
  func_entry->getInstructions(insns);
  for (auto i : insns) {
    // Here we should reuse stack analysis to allow writes to known stack
    // location. But it is going to be a future optimization.
    if (i.writesMemory())
      return false;
  }

  eb.clear();
  cfg->getExitBasicBlock(eb);
  // The function entry block should have two edges,
  // one cond-taken, one cond-not-taken.
  //
  // One of the edge should point to an exit block
  // as the fast path exit block.
  std::vector<BPatch_edge*> edges;
  func_entry->getOutgoingEdges(edges);
  if (edges.size() != 2)
    return false;
  bool condTaken = false;
  bool condNotTaken = false;
  BPatch_basicBlock* fastExitBlock = NULL;
  for (auto e : edges) {
    if (e->getType() == CondJumpTaken)
      condTaken = true;
    if (e->getType() == CondJumpNottaken)
      condNotTaken = true;
    BPatch_basicBlock* b = e->getTarget();
    if (std::find(eb.begin(), eb.end(), b) != eb.end()) {
      fastExitBlock = b;
    } else {
      entry = b;
    }
  }
  if (!condTaken || !condNotTaken || fastExitBlock == NULL || entry == NULL)
    return false;

  // Function entry block cannot have intra-procedural incoming edges.
  edges.clear();
  func_entry->getIncomingEdges(edges);
  if (edges.size() > 0)
    return false;

  // The slow path entry should only have entry block as source.
  // Otherwise, the stack push instrumentation will be executed multiple times.
  edges.clear();
  entry->getIncomingEdges(edges);
  if (edges.size() != 1)
    return false;

  // The fast path exit block should contain one return instruction.
  // This condition makes sure that our shadow stack instrumentation can
  // find the correct location for return address.
  // TODO: relax this condition later.
  insns.clear();
  fastExitBlock->getInstructions(insns);
  if (insns.size() != 1)
    return false;
  if (insns[0].getCategory() != InstructionAPI::c_ReturnInsn)
    return false;

  // Excluding the function entry block,
  // each block that is a source block of the fast exit block
  // should be instrumented with shadow stack pop.
  edges.clear();
  fastExitBlock->getIncomingEdges(edges);
  for (auto e : edges) {
    if (e->getSource() != func_entry) {
      BPatch_basicBlock* slowPathExit = e->getSource();
      // If the slow path exit has more than one outgoing edges,
      // we cannot instrument at its exit, because stack pop operation
      // can potentially be performed mulitple times.
      std::vector<BPatch_edge*> out_edges;
      slowPathExit->getOutgoingEdges(out_edges);
      if (out_edges.size() != 1)
        return false;
      exits.push_back(e->getSource());
    }
  }

  // In addition, we need to instrument all non-fast-exit exit blocks.
  for (auto b : eb)
    if (b != fastExitBlock)
      exits.push_back(b);
  return true;
}

void AddInlineHint(BPatch_function* function, const litecfi::Parser& parser) {
  Address entry = (Address)(function->getBaseAddr());
  parser.parser->addInliningEntry(entry);
}

bool Skippable(BPatch_function* function, FuncSummary* summary) {
  if (summary != nullptr && summary->safe) {
    StdOut(Color::RED, FLAGS_vv)
        << "      Skipping function : " << function->getName() << Endl;
    return true;
  }

  if (skip_addrs.find((Address)(function->getBaseAddr())) != skip_addrs.end()) {
    StdOut(Color::RED, FLAGS_vv)
        << "      Skipping function (user's skip list): " << function->getName()
        << Endl;

    return true;
  }
  return false;
}

void InstrumentFunction(BPatch_function* function,
                        const litecfi::Parser& parser, PatchMgr::Ptr patcher,
                        const std::map<uint64_t, FuncSummary*>& analyses) {
  StdOut(Color::YELLOW, FLAGS_vv)
      << "     Function : " << Dyninst::PatchAPI::convert(function)->name()
      << Endl;

  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  BPatch_nullExpr nopSnippet;
  vector<BPatch_point*> points;
  FuncSummary* summary = nullptr;

  if (FLAGS_shadow_stack == "light") {
    auto it = analyses.find(static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(function->getBaseAddr())));
    if (it != analyses.end()) {
      summary = (*it).second;
    }

    // Check if this function is safe to skip and do so if it is.
    if (Skippable(function, summary))
      return;

    // Add inlining hint so that writeFile may inline small leaf functions.
    AddInlineHint(function, parser);

    // Apply fast path optimization if applicable.
    BPatch_basicBlock* condNotTakenEntry = NULL;
    vector<BPatch_basicBlock*> condNotTakenExits;
    if (CheckFastPathFunction(condNotTakenEntry, condNotTakenExits, function)) {
      fastPathFunction++;
      StdOut(Color::RED, FLAGS_vv)
          << "      Optimized fast path instrumentation for function at 0x"
          << std::hex << (uint64_t)function->getBaseAddr() << Endl;
      /* If the function has the following shape:
       * entry:
       *    code that does not writes memory
       *    jz A   (or other conditional jump)
       *    some complicated code
       * A: ret
       *
       * Then we do not need to instrument the fast path: entry -> ret.
       * We can instrument the entry and exit of the "some complicated code",
       * which is the slow path.
       */

      // Instrument slow paths entry with stack push operation
      // and nop snippet, which enables instrumentation frame spec.
      BPatch_point* push_point = condNotTakenEntry->findEntryPoint();
      Snippet::Ptr stack_push =
          StackPushSnippet::create(new StackPushSnippet(summary));
      PatchAPI::convert(push_point, BPatch_callBefore)->pushBack(stack_push);
      points.push_back(push_point);
      binary_edit->insertSnippet(nopSnippet, points, BPatch_callBefore,
                                 BPatch_lastSnippet, &is_empty);

      // Instrument slow paths exits with stack pop operations
      // and nop snippet, which enables instrumentation frame spec.
      points.clear();
      Snippet::Ptr stack_pop =
          StackPopSnippet::create(new StackPopSnippet(summary));
      for (auto b : condNotTakenExits) {
        BPatch_point* pop_point = b->findExitPoint();
        PatchAPI::convert(pop_point, BPatch_callAfter)->pushBack(stack_pop);
        points.push_back(pop_point);
      }
      binary_edit->insertSnippet(nopSnippet, points, BPatch_callAfter,
                                 BPatch_lastSnippet, &is_empty);
      return;
    }
  }

  // Either we are in 'full' instrumentation mode or in 'light' mode but none of
  // the optimizations worked out. Carry out regular entry-exit shadow stack
  // instrumentation.
  Snippet::Ptr stack_push =
      StackPushSnippet::create(new StackPushSnippet(summary));
  InsertSnippet(function, Point::FuncEntry, stack_push, patcher);

  Snippet::Ptr stack_pop =
      StackPopSnippet::create(new StackPopSnippet(summary));
  InsertSnippet(function, Point::FuncExit, stack_pop, patcher);

  function->getEntryPoints(points);
  binary_edit->insertSnippet(nopSnippet, points, BPatch_callBefore,
                             BPatch_lastSnippet, &is_empty);

  points.clear();
  function->getExitPoints(points);
  binary_edit->insertSnippet(nopSnippet, points, BPatch_callAfter,
                             BPatch_lastSnippet, &is_empty);
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

void InstrumentInitFunction(BPatch_function* function,
                            const litecfi::Parser& parser) {
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);
  BPatch_Vector<BPatch_snippet*> args;

  auto stack_init_fn = FindFunctionByName(parser.image, kShadowStackInitFn);

  BPatch_funcCallExpr stack_init(*stack_init_fn, args);
  std::vector<BPatch_point*>* entries = function->findPoint(BPatch_entry);
  BPatchSnippetHandle* handle = nullptr;
  handle = binary_edit->insertSnippet(stack_init, *entries, BPatch_callBefore,
                                      BPatch_lastSnippet, &is_init);
  DCHECK(handle != nullptr)
      << "Failed to instrument init function for stack initialization.";
}

static void GetIFUNCs(BPatch_module* module,
                      std::set<Dyninst::Address>& addrs) {
  SymtabAPI::Module* sym_mod = SymtabAPI::convert(module);
  std::vector<SymtabAPI::Symbol*> ifuncs;

  // Dyninst represents IFUNC as ST_INDIRECT.
  sym_mod->getAllSymbolsByType(ifuncs, SymtabAPI::Symbol::ST_INDIRECT);
  for (auto sit = ifuncs.begin(); sit != ifuncs.end(); ++sit) {
    addrs.insert((Address)(*sit)->getOffset());
  }
}

void InstrumentModule(BPatch_module* module, const litecfi::Parser& parser,
                      PatchMgr::Ptr patcher,
                      const std::map<uint64_t, FuncSummary*>& analyses) {
  std::vector<BPatch_function*>* functions = module->getProcedures();

  std::set<Dyninst::Address> ifuncAddrs;
  GetIFUNCs(module, ifuncAddrs);

  for (auto it = functions->begin(); it != functions->end(); it++) {
    BPatch_function* function = *it;

    ParseAPI::Function* f = ParseAPI::convert(function);
    if (f->retstatus() == ParseAPI::NORETURN)
      continue;

    // We should only instrument functions in .text.
    ParseAPI::CodeRegion* codereg = f->region();
    ParseAPI::SymtabCodeRegion* symRegion =
        dynamic_cast<ParseAPI::SymtabCodeRegion*>(codereg);
    assert(symRegion);
    SymtabAPI::Region* symR = symRegion->symRegion();
    if (symR->getRegionName() != ".text")
      continue;

    InstrumentFunction(function, parser, patcher, analyses);
  }
}

void InstrumentCodeObject(BPatch_object* object, const litecfi::Parser& parser,
                          PatchMgr::Ptr patcher) {
  if (!IsSharedLibrary(object)) {
    StdOut(Color::GREEN, FLAGS_vv) << "\n  >> Instrumenting main application "
                                   << object->pathName() << Endl;
  } else {
    StdOut(Color::GREEN, FLAGS_vv)
        << "\n    Instrumenting " << object->pathName() << Endl;
  }

  if (FLAGS_threat_model == "trust_system" && IsSystemCode(object)) {
    return;
  }

  std::vector<BPatch_module*> modules;
  object->modules(modules);

  std::map<uint64_t, FuncSummary*> analyses;
  // Do the static analysis on this code and obtain skippable functions.
  if (FLAGS_shadow_stack == "light") {
    CodeObject* co = Dyninst::ParseAPI::convert(object);
    co->parse();

    PassManager* pm = new PassManager;
    pm->AddPass(new CallGraphPass())
        ->AddPass(new LargeFunctionFilterPass())
        ->AddPass(new IntraProceduralMemoryAnalysis())
        ->AddPass(new InterProceduralMemoryAnalysis())
        ->AddPass(new DeadRegisterAnalysisPass());
    std::set<FuncSummary*> summaries = pm->Run(co);

    for (auto f : summaries) {
      analyses[f->func->addr()] = f;
    }
  }

  for (auto it = modules.begin(); it != modules.end(); it++) {
    char modname[2048];
    BPatch_module* module = *it;
    module->getName(modname, 2048);

    InstrumentModule(module, parser, patcher, analyses);
  }
}

void SetupInstrumentationSpec() {
  // Suppose we instrument a call to stack init at entry of A;
  // If A does not use r11, we dont need to save r11 (_start does not).
  is_init.trampGuard = false;
  is_init.redZone = false;
  is_init.saveRegs.push_back(x86_64::rax);
  is_init.saveRegs.push_back(x86_64::rdx);

  is_empty.trampGuard = false;
  is_empty.redZone = false;
}

void Instrument(std::string binary, const litecfi::Parser& parser) {
  StdOut(Color::BLUE, FLAGS_vv) << "\n\nInstrumentation Pass" << Endl;
  StdOut(Color::BLUE, FLAGS_vv) << "====================" << Endl;

  StdOut(Color::BLUE) << "+ Instrumenting the binary..." << Endl;

  if (FLAGS_skip_list != "") {
    std::ifstream infile(FLAGS_skip_list, std::fstream::in);
    Address addr;
    while (infile >> std::hex >> addr)
      skip_addrs.insert(addr);
  }

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  PatchMgr::Ptr patcher = Dyninst::PatchAPI::convert(parser.app);
  BPatch_binaryEdit* binary_edit = ((BPatch_binaryEdit*)parser.app);

  SetupInstrumentationSpec();

  for (auto it = objects.begin(); it != objects.end(); it++) {
    BPatch_object* object = *it;

    // Skip other shared libraries for now.
    if (!FLAGS_libs && IsSharedLibrary(object)) {
      continue;
    }

    InstrumentCodeObject(object, parser, patcher);
  }

  if (FLAGS_output.empty()) {
    binary_edit->writeFile((binary + "_cfi").c_str());
  } else {
    binary_edit->writeFile(FLAGS_output.c_str());
  }
  StdOut(Color::BLUE) << "Fast path functions " << std::dec << fastPathFunction
                      << Endl;
}
