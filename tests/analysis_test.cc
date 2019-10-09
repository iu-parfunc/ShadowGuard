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

using Dyninst::ParseAPI::CodeObject;
using Dyninst::ParseAPI::SymtabCodeSource;
using std::string;

DEFINE_bool(vv, false, "Log verbose output.");
DEFINE_string(stats, "", "File to log statistics related to static anlaysis.");

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
      ->AddPass(new LargeFunctionFilterPass())
      ->AddPass(new IntraProceduralMemoryAnalysis())
      ->AddPass(new InterProceduralMemoryAnalysis())
      ->AddPass(new DeadRegisterAnalysisPass());
  return pm;
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

TEST(AnalysisTest, TestsSafeLeaf) {
  string binary = "bazel-bin/tests/safe_leaf";
  string function = "safe_leaf_fn";

  auto summary = GetSummary(Analyse(binary), function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, true);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);
}

TEST(AnalysisTest, TestsSafeNonLeaf) {
  string binary = "bazel-bin/tests/safe_non_leaf";
  string function = "safe_non_leaf_fn";

  auto summaries = Analyse(binary);
  FuncSummary *summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, true);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);

  function = "non_leaf_fn";
  summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, true);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);

  function = "leaf_fn";
  summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, true);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);
}

TEST(AnalysisTest, TestsUnsafeLeaf) {
  string binary = "bazel-bin/tests/unsafe_leaf";
  string function = "unsafe_leaf_fn";

  auto summaries = Analyse(binary);
  FuncSummary *summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, true);
  EXPECT_EQ(summary->child_writes, false);
}

TEST(AnalysisTest, TestsUnsafeNonLeaf) {
  string binary = "bazel-bin/tests/unsafe_non_leaf";
  string function = "unsafe_non_leaf_fn";

  auto summaries = Analyse(binary);
  FuncSummary *summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, true);

  function = "non_leaf_fn";
  summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, true);

  function = "ns_leaf_fn";
  summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, true);
  EXPECT_EQ(summary->child_writes, false);
}

TEST(AnalysisTest, TestsIndirectCall) {
  string binary = "bazel-bin/tests/indirect_call";
  string function = "indirect_call";

  auto summaries = Analyse(binary);
  FuncSummary *summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);
  EXPECT_EQ(summary->assume_unsafe, true);
}

TEST(AnalysisTest, TestsIndirectCallTree) {
  string binary = "bazel-bin/tests/indirect_call_tree";
  string function = "indirect_call_tree";

  auto summaries = Analyse(binary);
  FuncSummary *summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, true);
  EXPECT_EQ(summary->assume_unsafe, false);

  function = "indirect_call";
  summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);
  EXPECT_EQ(summary->assume_unsafe, true);
}

TEST(AnalysisTest, TestsPltCall) {
  string binary = "bazel-bin/tests/plt_call";
  string function = "plt_call";

  auto summaries = Analyse(binary);
  FuncSummary *summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);
  EXPECT_EQ(summary->assume_unsafe, true);
}

TEST(AnalysisTest, TestsPltCallTree) {
  string binary = "bazel-bin/tests/plt_call_tree";
  string function = "plt_call_tree";

  auto summaries = Analyse(binary);
  FuncSummary *summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, true);
  EXPECT_EQ(summary->assume_unsafe, false);

  function = "plt_call";
  summary = GetSummary(summaries, function);
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->safe, false);
  EXPECT_EQ(summary->self_writes, false);
  EXPECT_EQ(summary->child_writes, false);
  EXPECT_EQ(summary->assume_unsafe, true);
}
