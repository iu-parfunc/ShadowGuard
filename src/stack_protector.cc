
#include <fstream>
#include <map>
#include <vector>

#include "BPatch.h"
#include "BPatch_function.h"
#include "BPatch_object.h"
#include "CodeObject.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "parse.h"
#include "utils.h"

DEFINE_string(output, "func.txt",
              "\n List of functions with stack protector enabled.\n");

struct Summary {
  uint64_t total_functions;
  uint64_t protected_functions;
  double protected_ratio;
};

bool IsStackCookieAccessed(Dyninst::ParseAPI::Block* block) {
  std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;
  block->getInsns(insns);

  for (auto const& ins : insns) {
    std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> read;
    std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;

    ins.second.getReadSet(read);
    ins.second.getWriteSet(written);

    for (auto const& read_register : read) {
      if (read_register->format() == "FS") {
        return true;
      }
    }

    for (auto const& written_register : written) {
      if (written_register->format() == "FS") {
        return true;
      }
    }
  }

  return false;
}

Summary DiscoverStackProtectedFunctions(Parser* parser) {
  std::vector<BPatch_object*> objects;
  parser->image->getObjects(objects);

  std::vector<std::string> protected_functions;

  long total_functions = 0;
  for (auto object : objects) {
    if (IsSharedLibrary(object))
      continue;

    Dyninst::ParseAPI::CodeObject* code_object =
        Dyninst::ParseAPI::convert(object);

    for (auto function : code_object->funcs()) {
      total_functions++;

      Dyninst::ParseAPI::Block* entry = function->entry();

      if (IsStackCookieAccessed(entry))
        protected_functions.push_back(function->name());
    }
  }

  std::ofstream outfile(FLAGS_output);

  for (std::string f : protected_functions) {
    outfile << f << "\n";
  }

  outfile.close();

  return {total_functions, protected_functions.size(),
          (double)protected_functions.size() / total_functions};
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);

  std::string usage("Usage : ./stack-protector <flags> binary");
  gflags::SetUsageMessage(usage);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string binary(argv[1]);

  Parser* parser;
  parser = InitParser(binary, /* libs */ false, /* sanitize */ true);

  Summary summary = DiscoverStackProtectedFunctions(parser);

  std::cout << "Protected functions : " << summary.protected_functions << "\n";
  std::cout << "Total functions : " << summary.total_functions << "\n";
  std::cout << "Protected (%) : " << summary.protected_ratio * 100 << "\n";

  return 0;
}
