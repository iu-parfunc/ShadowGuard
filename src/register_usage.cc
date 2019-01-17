
#include "register_usage.h"

#include <string>

#include "Instruction.h"

std::string Normalize(std::string reg) {
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

int ExtractNumericPostFix(std::string reg) {
  std::string post_fix =
      reg.substr(reg.size() - 2);  // Numeric Postfix can be one or two digits

  if (std::isdigit(post_fix[0])) {
    return std::stoi(post_fix, nullptr);
  } else if (std::isdigit(post_fix[1])) {
    return std::stoi(post_fix.substr(1), nullptr);
  }

  return -1;
}

void PopulateUnusedAvxMask(const std::set<std::string>& used,
                           RegisterUsageInfo* const info) {
  bool used_mask[30] = {false};  // Used register mask
  for (const std::string& reg : used) {
    if (!reg.compare(0, 1, "Y")) {  // Register is AVX2
      // Extract integer post fix
      int register_index = ExtractNumericPostFix(reg);
      // DCHECK(register_index > 0 && register_index < 16);
      used_mask[register_index * 2] = true;
      used_mask[register_index * 2 + 1] = true;
    } else if (!reg.compare(0, 1, "X")) {  // Register is AVX
      int register_index = ExtractNumericPostFix(reg);
      // DCHECK(register_index > 0 && register_index < 16)
      used_mask[register_index * 2] = true;
    }
  }

  for (int i = 0; i < 30; i++) {
    info->unused_avx_mask.push_back(!used_mask[i]);
  }
}

void PopulateUnusedMmxMask(const std::set<std::string>& used,
                           RegisterUsageInfo* const info) {
  bool used_mask[8] = {false};  // Used register mask
  for (const std::string& reg : used) {
    if (reg.compare(0, 1, "M")) {  // Register is MMX
      int register_index = ExtractNumericPostFix(reg);
      // DCHECK(register_index > 0)
      used_mask[register_index] = true;
    }
  }

  for (int i = 0; i < 8; i++) {
    info->unused_mmx_mask.push_back(!used_mask[i]);
  }
}

void PopulateUnusedGprMask(const std::set<std::string>& used,
                           RegisterUsageInfo* const info) {
  bool used_mask[15] = {false};  // Used register mask
  // TODO(chamibuddhika) Complete this
}

RegisterUsageInfo FindUnusedRegisterInfo(Dyninst::ParseAPI::CodeObject* co) {
  std::set<std::string> all_regs;
  std::set<std::string> used;

  auto fit = co->funcs().begin();
  for (; fit != co->funcs().end(); ++fit) {
    Dyninst::ParseAPI::Function* f = *fit;
    if (co->cs()->linkage().find(f->addr()) != co->cs()->linkage().end())
      continue;

    std::map<Dyninst::Offset, Dyninst::InstructionAPI::Instruction> insns;
    for (auto b : f->blocks()) {
      b->getInsns(insns);

      for (auto const& ins : insns) {
        std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> regsRead;
        std::set<Dyninst::InstructionAPI::RegisterAST::Ptr> regsWritten;
        ins.second.getReadSet(regsRead);
        ins.second.getWriteSet(regsWritten);

        for (auto it = regsRead.begin(); it != regsRead.end(); it++) {
          used.insert(Normalize((*it)->format()));
        }

        for (auto it = regsWritten.begin(); it != regsWritten.end(); it++) {
          used.insert(Normalize((*it)->format()));
        }
      }
    }
  }

  RegisterUsageInfo info;
  PopulateUnusedAvxMask(used, &info);
  PopulateUnusedMmxMask(used, &info);

  return info;
}
