#include <iostream>
#include <string>

#include "Absloc.h"
#include "AbslocInterface.h"
#include "CFG.h"
#include "CodeObject.h"
#include "CodeSource.h"
#include "Expression.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "Register.h"
#include "src/pass_manager.h"
#include "src/passes.h"
#include "gtest/gtest.h"

CodeObject *GetCodeObject(char *binary) {
  SymtabCodeSource *sts = new SymtabCodeSource(binary);
  CodeObject *co = new CodeObject(sts);
  co->parse();
  return co;
}

bool MatchName(std::string in_str, std::string matched) {
  return in_str.find(matched) != std::string::npos;
}

TEST(AnalysisTest, TestsSimpleLeaf) {
  PassManager *pm = new PassManager;
  pm->AddPass(new CallGraphPass())
      ->AddPass(new LeafAnalysisPass())
      ->AddPass(new StackAnalysisPass())
      ->AddPass(new NonLeafSafeWritesPass())
      ->AddPass(new DeadRegisterAnalysisPass());

  char binary[] = "bazel-bin/tests/simple_leaf";
  std::set<FuncSummary *> summaries = pm->Run(GetCodeObject(binary));

  for (auto s : summaries) {
    if (MatchName(s->func->name(), "leaf_fn")) {
      EXPECT_EQ(s->safe, true);
      return;
    }
  }

  ASSERT_TRUE(false);
}
