
#include "BPatch_function.h"
#include "CodeObject.h"
#include "parse.h"

std::string NormalizeRegisterName(std::string reg) {
  if (!reg.compare(0, 1, "E")) {
    return "R" + reg.substr(1);
  }

  if (!(reg.compare("AX") && reg.compare("AH") && reg.compare("AL"))) {
    return "RAX";
  } else if (!(reg.compare("BX") && reg.compare("BH") && reg.compare("BL"))) {
    return "RBX";
  } else if (!(reg.compare("CX") && reg.compare("CH") && reg.compare("CL"))) {
    return "RCX";
  } else if (!(reg.compare("DX") && reg.compare("DH") && reg.compare("DL"))) {
    return "RDX";
  } else if (!reg.compare("SI")) {
    return "RSI";
  } else if (!reg.compare("DI")) {
    return "RDI";
  } else if (!reg.compare("BP")) {
    return "RBP";
  } else if (!reg.compare("SP")) {
    return "RSP";
  } else if (!reg.compare(0, 1, "R") && isdigit(reg.substr(1, 1).at(0)) &&
             (std::isalpha(reg.substr(reg.size() - 1).at(0)))) {
    if (std::isdigit(reg.substr(2, 1).at(0))) {
      return "R" + reg.substr(1, 2);
    }

    return "R" + reg.substr(1, 1);
  }

  return reg;
}

void PopulateUsedRegistersInFunction(
    Dyninst::ParseAPI::Function* const function,
    std::set<std::string>* const used) {
  std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;

  std::set<std::string> regs;

  int block_counter = 0;
  int ins_counter = 0;
  for (auto b : function->blocks()) {
    std::cout << "\n-- Block: " << ++block_counter << "--\n";
    b->getInsns(insns);

    for (auto const& ins : insns) {
      std::cout << "Instruction : " << ++ins_counter << "\n";
      std::cout << "  Instruction : " << ins.second.format() << "\n";
      std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> read;
      std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> written;
      ins.second.getReadSet(read);
      ins.second.getWriteSet(written);

      for (auto const& read_register : read) {
        std::string normalized_name =
            NormalizeRegisterName(read_register->format());
        regs.insert(normalized_name);
        used->insert(normalized_name);

        std::cout << "  Used register : " << read_register->format() << "\n";
        std::cout << "  Used register (normalized) : " << normalized_name
                  << "\n\n";
      }

      for (auto const& written_register : written) {
        std::string normalized_name =
            NormalizeRegisterName(written_register->format());
        regs.insert(normalized_name);
        used->insert(normalized_name);

        std::cout << "  Used register : " << written_register->format() << "\n";
        std::cout << "  Used register (normalized) : " << normalized_name
                  << "\n\n";
      }
    }
  }
}

int main(int argc, char* argv[]) {
  std::string binary(argv[1]);
  Parser parser = InitParser(binary);

  std::vector<BPatch_object*> objects;
  parser.image->getObjects(objects);

  for (auto object : objects) {
    if (!IsSharedLibrary(object)) {
      // This is the program text.
      Dyninst::ParseAPI::CodeObject* code_object =
          Dyninst::ParseAPI::convert(object);
      // code_object->parse();

      for (auto function : code_object->funcs()) {
        if (function->name() != "_Z3bazv") {
          // We only want to analyze baz
          continue;
        }

        std::cout << "Function : " << function->name() << "\n\n";

        std::set<std::string> registers;
        PopulateUsedRegistersInFunction(function, &registers);
      }
    }
  }
}
