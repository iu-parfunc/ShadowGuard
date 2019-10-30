
#ifndef LITECFI_REGISTER_UTILS_H_
#define LITECFI_REGISTER_UTILS_H_

#include <string>

inline std::string NormalizeRegisterName(std::string reg_with_arch) {
  std::string arch = "x86_64::";

  // Remove architecture prefix (x86_64::)
  std::string reg = reg_with_arch.substr(arch.length(), std::string::npos);
  if (!reg.compare(0, 1, "e")) {
    return arch + "r" + reg.substr(1);
  }

  if (!(reg.compare("ax") && reg.compare("ah") && reg.compare("al"))) {
    return arch + "rax";
  } else if (!(reg.compare("bx") && reg.compare("bh") && reg.compare("bl"))) {
    return arch + "rbx";
  } else if (!(reg.compare("cx") && reg.compare("ch") && reg.compare("cl"))) {
    return arch + "rcx";
  } else if (!(reg.compare("dx") && reg.compare("dh") && reg.compare("dl"))) {
    return arch + "rdx";
  } else if (!reg.compare("si")) {
    return arch + "rsi";
  } else if (!reg.compare("di")) {
    return arch + "rdi";
  } else if (!reg.compare("bp")) {
    return arch + "rbp";
  } else if (!reg.compare("sp")) {
    return arch + "rsp";
  } else if (!reg.compare(0, 1, "r") && isdigit(reg.substr(1, 1).at(0)) &&
             (std::isalpha(reg.substr(reg.size() - 1).at(0)))) {
    if (std::isdigit(reg.substr(2, 1).at(0))) {
      return arch + "r" + reg.substr(1, 2);
    }

    return arch + "r" + reg.substr(1, 1);
  }

  return reg_with_arch;
}

#endif  // LITECFI_REGISTER_UTILS_H_
