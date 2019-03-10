
#include "codegen.h"

#include <stdlib.h>
#include <sys/stat.h>

#include <fstream>

#include "jit.h"
#include "utils.h"

std::string Header() {
  std::string header = "";
  header += ".intel_syntax noprefix\n\n";
  header +=
      "#if defined(__linux) && defined(__GNUC__) && defined(__x86_64__)\n";
  header += "# define ASM_HIDDEN(symbol) .hidden symbol\n";
  header += "# define ASM_TYPE_FUNCTION(symbol) .type symbol, @function\n";
  header += "# define ASM_SIZE(symbol) .size symbol, .-symbol\n";
  header += "# define ASM_SYMBOL(symbol) symbol\n";
  header += "# define NO_EXEC_STACK_DIRECTIVE .section "
            ".note.GNU-stack,\"\",%progbits\n\n";
  header += "# define CFI_DEF_CFA_OFFSET(n) .cfi_def_cfa_offset n\n";
  header += "# define CFI_STARTPROC .cfi_startproc\n";
  header += "# define CFI_ENDPROC .cfi_endproc\n";
  header += "#else\n";
  header += "# We only support x86_64 on Linux currently\n";
  header += "#endif\n\n";

  return header;
}

std::string Footer() { return "NO_EXEC_STACK_DIRECTIVE"; }

std::string FunctionProlog(std::string name) {
  std::string prolog = "";
  prolog += "  .globl ASM_SYMBOL(" + name + ")\n";
  prolog += "  ASM_TYPE_FUNCTION(" + name + ")\n";
  prolog += "ASM_SYMBOL(" + name + "):\n";
  prolog += "CFI_STARTPROC\n";

  return prolog;
}

std::string FunctionEpilog(std::string name) {
  std::string epilog = "";
  epilog += "ret\n";
  epilog += "ASM_SIZE(" + name + ")\n";
  epilog += "CFI_ENDPROC\n\n";

  return epilog;
}

std::string GenerateFunction(std::string fn_name, RegisterUsageInfo& info,
                             void (*codegen)(RegisterUsageInfo,
                                             AssemblerHolder&),
                             std::string overflow_slot = "") {
  std::string prolog = FunctionProlog(fn_name);

  /* Generate function body */
  AssemblerHolder ah;
  codegen(info, ah);

  std::string code(ah.GetStringLogger()->getString());

  std::string epilog = FunctionEpilog(fn_name);

  return prolog + code + overflow_slot + epilog;
}

std::string CodegenStackInit(RegisterUsageInfo info) {
  return GenerateFunction(kStackInitFunction, info, JitStackInit);
}

std::string CodegenStackPush(RegisterUsageInfo info) {
  std::string overflow_slot = "";
  overflow_slot += "call litecfi_overflow_stack_push@plt\n";
  return GenerateFunction(kStackPushFunction, info, JitStackPush,
                          overflow_slot);
}

std::string CodegenStackPop(RegisterUsageInfo info) {
  std::string overflow_slot = "";
  overflow_slot += "call litecfi_overflow_stack_pop@plt\n";
  return GenerateFunction(kStackPopFunction, info, JitStackPop);
}

std::string Codegen(RegisterUsageInfo info) {
  std::string temp_dir = "/tmp/.litecfi_tmp";

  // Remove any existing temporary files
  std::string remove_temp_dir = "rm -rf " + temp_dir;
  system(remove_temp_dir.c_str());

  if (mkdir(temp_dir.c_str(), 0777) != 0) {
    return "";
  }

  std::string stack_push = Header() + CodegenStackPush(info) + Footer();
  std::ofstream push(temp_dir + "/" + "__litecfi_stack_push_x86_64.S");
  push << stack_push;

  std::string stack_pop = Header() + CodegenStackPop(info) + Footer();
  std::ofstream pop(temp_dir + "/" + "__litecfi_stack_pop_x86_64.S");
  pop << stack_pop;

  std::string soname = "libstack.so";

  std::string compile_so =
      "gcc -fpic -shared -o " + soname + " " + temp_dir + "/" + "*.S";

  if (system(compile_so.c_str()) != 0) {
    return "";
  }

  /*
  std::string remove_temp_dir = "rm -r " + temp_dir;
  system(remove_temp_dir.c_str());
  */

  return soname;
}
