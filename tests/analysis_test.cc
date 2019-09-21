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
#include "gflags/gflags.h"
#include "src/pass_manager.h"
#include "src/passes.h"
#include "gtest/gtest.h"

using std::string;

DEFINE_bool(vv, false, "Log verbose output.");

CodeObject *GetCodeObject(const char *binary) {
  SymtabCodeSource *sts = new SymtabCodeSource(const_cast<char *>(binary));
  CodeObject *co = new CodeObject(sts);
  co->parse();
  return co;
}

bool MatchName(string in_str, string matched) {
  return in_str.find(matched) != string::npos;
}

PassManager *GetPassManager() {
  PassManager *pm = new PassManager;
  pm->AddPass(new CallGraphPass())
      ->AddPass(new LeafAnalysisPass())
      ->AddPass(new StackAnalysisPass())
      ->AddPass(new NonLeafSafeWritesPass())
      ->AddPass(new DeadRegisterAnalysisPass());
  return pm;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
bool GetFunctionSafety(string binary, string function) {
  PassManager *pm = GetPassManager();
  std::set<FuncSummary *> summaries = pm->Run(GetCodeObject(binary.c_str()));

  for (auto s : summaries) {
    if (MatchName(s->func->name(), function)) {
      return s->safe;
    }
  }

  EXPECT_TRUE(false);
}
#pragma GCC diagnostic pop

TEST(AnalysisTest, TestsSafeLeaf) {
  string binary = "bazel-bin/tests/safe_leaf";
  string function = "safe_leaf_fn";

  EXPECT_EQ(GetFunctionSafety(binary, function), true);
}

TEST(AnalysisTest, TestsSafeNonLeaf) {
  string binary = "bazel-bin/tests/safe_non_leaf";
  string function = "safe_non_leaf_fn";

  EXPECT_EQ(GetFunctionSafety(binary, function), true);
}

TEST(AnalysisTest, TestsUnsafeLeaf) {
  string binary = "bazel-bin/tests/unsafe_leaf";
  string function = "unsafe_leaf_fn";

  EXPECT_EQ(GetFunctionSafety(binary, function), false);
}

TEST(AnalysisTest, TestsUnsafeNonLeaf) {
  string binary = "bazel-bin/tests/unsafe_non_leaf";
  string function = "unsafe_non_leaf_fn";

  EXPECT_EQ(GetFunctionSafety(binary, function), false);
}

TEST(AnalysisTest, TestsIndirectCall) {
  string binary = "bazel-bin/tests/indirect_call";
  string function = "indirect_call";

  EXPECT_EQ(GetFunctionSafety(binary, function), false);
}

TEST(AnalysisTest, TestsIndirectCallTree) {
  string binary = "bazel-bin/tests/indirect_call_tree";
  string function = "indirect_call_tree";

  EXPECT_EQ(GetFunctionSafety(binary, function), false);
}

TEST(AnalysisTest, TestsPltCall) {
  string binary = "bazel-bin/tests/plt_call";
  string function = "plt_call";

  EXPECT_EQ(GetFunctionSafety(binary, function), false);
}

TEST(AnalysisTest, TestsPltCallTree) {
  string binary = "bazel-bin/tests/plt_call_tree";
  string function = "plt_call_tree";

  EXPECT_EQ(GetFunctionSafety(binary, function), false);
}
