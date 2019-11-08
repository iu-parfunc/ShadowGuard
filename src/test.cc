#include <iostream>
#include <string>

#include "CFG.h"
#include "CodeObject.h"
#include "CodeSource.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Register.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "src/pass_manager.h"
#include "src/passes.h"

DEFINE_bool(vv, false, "Log verbose output.");
DEFINE_string(stats, "", "File to log statistics related to static anlaysis.");

using namespace Dyninst;
using namespace ParseAPI;
using namespace InstructionAPI;

using std::string;

using Dyninst::ParseAPI::CodeObject;
using Dyninst::ParseAPI::SymtabCodeSource;

PassManager *GetPassManager() {
  PassManager *pm = new PassManager;
  pm->AddPass(new CallGraphAnalysis())
      ->AddPass(new LargeFunctionFilter())
      ->AddPass(new IntraProceduralMemoryAnalysis())
      ->AddPass(new InterProceduralMemoryAnalysis())
      ->AddPass(new CFGAnalysis())
      ->AddPass(new CFGStatistics())
      ->AddPass(new LowerInstrumentation())
      /*
      ->AddPass(new LinkParentsOfCFG())
      ->AddPass(new CoalesceIngressInstrumentation())
      ->AddPass(new CoalesceEgressInstrumentation())
      */
      ->AddPass(new ValidateCFG())
      ->AddPass(new LoweringStatistics())
      ->AddPass(new DeadRegisterAnalysis());
  return pm;
}

CodeObject *GetCodeObject(const char *binary) {
  SymtabCodeSource *sts = new SymtabCodeSource(const_cast<char *>(binary));
  CodeObject *co = new CodeObject(sts);
  co->parse();
  return co;
}

FuncSummary *GetSummary(const std::set<FuncSummary *> &summaries,
                        string function) {
  for (auto s : summaries) {
    if (s->func->name() == function) {
      return s;
    }
  }

  return nullptr;
}

std::set<FuncSummary *> Analyse(string binary) {
  PassManager *pm = GetPassManager();
  return pm->Run(GetCodeObject(binary.c_str()));
}

int main(int argc, char *argv[]) {
  google::InitGoogleLogging(argv[0]);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);
  string function = "foo";

  auto summary = GetSummary(Analyse(binary), function);

  StdOut(Color::GREEN) << "Function Summary:\n";
  StdOut(Color::GREEN) << "  Name : " << function << "\n";
  StdOut(Color::GREEN) << "  Original nodes : "
                       << summary->stats->n_original_nodes << "\n";
  StdOut(Color::GREEN) << "  Lowered nodes : "
                       << summary->stats->n_lowered_nodes << "\n";
  StdOut(Color::GREEN) << "  Increase : " << summary->stats->increase << "\n\n";
  StdOut(Color::GREEN) << "  Safe paths : " << summary->stats->safe_paths
                       << "\n";
  StdOut(Color::GREEN) << "  Unsafe paths : " << summary->stats->unsafe_paths
                       << "\n";
  StdOut(Color::GREEN) << "  Safe ratio: " << summary->stats->safe_ratio << "\n"
                       << Endl;
}
