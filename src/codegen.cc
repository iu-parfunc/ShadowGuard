
#include "codegen.h"

#include <stdlib.h>
#include <sys/stat.h>

#include <fstream>

#include "jit.h"
#include "jit_internal.h"
#include "utils.h"

DECLARE_string(shadow_stack);
DECLARE_bool(stats);

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

std::string FunctionProlog(std::string name, bool global) {
  std::string prolog = "";
  if (global)
    prolog += "  .globl ASM_SYMBOL(" + name + ")\n";
  else
    prolog += "  ASM_HIDDEN(" + name + ")\n";
  prolog += "  ASM_TYPE_FUNCTION(" + name + ")\n";
  prolog += "ASM_SYMBOL(" + name + "):\n";
  prolog += "CFI_STARTPROC\n";

  return prolog;
}

std::string FunctionEpilog(std::string name) {
  std::string epilog = "";
  epilog += ".align 64\n";
  epilog += "ASM_SIZE(" + name + ")\n";
  epilog += "CFI_ENDPROC\n\n";

  return epilog;
}

std::string
GenerateFunction(std::string fn_name, RegisterUsageInfo& info,
                 std::string (*codegen)(RegisterUsageInfo, AssemblerHolder&),
                 std::string overflow_slot = "", bool global = true) {
  std::string prolog = FunctionProlog(fn_name, global);

  /* Generate function body */
  AssemblerHolder ah;
  std::string code = codegen(info, ah);

  std::string epilog = FunctionEpilog(fn_name);

  return prolog + code + overflow_slot + epilog;
}

std::string CodegenStackInit(RegisterUsageInfo info) {
  std::string overflow_slot = "ret\n";

  return GenerateFunction(kStackInitFunction, info, JitStackInit,
                          overflow_slot);
}

std::string CodegenStack(RegisterUsageInfo info) {
  std::string overflow_slot = "";

  // Overflow push slot
  overflow_slot += "push rax\n";
  overflow_slot += "push rdx\n";
  overflow_slot += "push rcx\n";
  overflow_slot += "push rdi\n";
  overflow_slot += "mov rdi, r10\n";

  if (FLAGS_stats) {
    overflow_slot += "call litecfi_overflow_stack_push_v3_stats@plt\n";
  } else {
    overflow_slot += "call litecfi_overflow_stack_push_v3@plt\n";
  }

  overflow_slot += "pop rdi\n";
  overflow_slot += "pop rcx\n";
  overflow_slot += "pop rdx\n";
  overflow_slot += "pop rax\n";
  overflow_slot += "ret\n";
  overflow_slot += ".align 32\n";

  // Overflow pop slot
  overflow_slot += "push rax\n";
  overflow_slot += "push rdx\n";
  overflow_slot += "push rcx\n";
  overflow_slot += "push rdi\n";
  overflow_slot += "mov rdi, r10\n";

  if (FLAGS_stats) {
    overflow_slot += "call litecfi_overflow_stack_pop_v3_stats@plt\n";
  } else {
    overflow_slot += "call litecfi_overflow_stack_pop_v3@plt\n";
  }

  overflow_slot += "pop rdi\n";
  overflow_slot += "pop rcx\n";
  overflow_slot += "pop rdx\n";
  overflow_slot += "pop rax\n";
  overflow_slot += "ret\n";
  overflow_slot += ".align 32\n";

  // Second overflow slot for push
  overflow_slot += "vmovq r11, xmm15\n";
  overflow_slot += "lea r11, [r11 - 64]\n";
  overflow_slot += "vmovq xmm15, r11\n";
  overflow_slot += "lea r11, [r11 - 64]\n";
  overflow_slot += "jmp r11\n";

  return GenerateFunction(kStackFunction, info, JitStack, overflow_slot, false);
}

std::string CodegenStackPush(RegisterUsageInfo info) {
  std::string overflow_slot = "";
  overflow_slot += "push rax\n";
  overflow_slot += "push rdx\n";
  overflow_slot += "push rcx\n";
  overflow_slot += "push rdi\n";
  overflow_slot += "mov rdi, r10\n";
  overflow_slot += "call litecfi_overflow_stack_push@plt\n";
  overflow_slot += "pop rdi\n";
  overflow_slot += "pop rcx\n";
  overflow_slot += "pop rdx\n";
  overflow_slot += "pop rax\n";
  overflow_slot += "ret\n";

  return GenerateFunction(kStackPushFunction, info, JitStackPush, overflow_slot,
                          false);
}

std::string CodegenStackPop(RegisterUsageInfo info) {
  std::string overflow_slot = "";
  overflow_slot += "push rax\n";
  overflow_slot += "push rdx\n";
  overflow_slot += "push rcx\n";
  overflow_slot += "push rdi\n";
  overflow_slot += "mov rdi, r10\n";
  overflow_slot += "call litecfi_overflow_stack_pop@plt\n";
  overflow_slot += "pop rdi\n";
  overflow_slot += "pop rcx\n";
  overflow_slot += "pop rdx\n";
  overflow_slot += "pop rax\n";
  overflow_slot += "ret\n";

  return GenerateFunction(kStackPopFunction, info, JitStackPop, overflow_slot,
                          false);
}

std::string CodegenEmptyFunction() {
  std::string prolog = FunctionProlog("litecfi_empty", true);

  std::string code = "ret\n";

  std::string epilog = "";
  epilog += "ASM_SIZE(litecfi_empty)\n";
  epilog += "CFI_ENDPROC\n\n";

  return prolog + code + epilog;
}

std::string Codegen(RegisterUsageInfo info) {
  std::string temp_dir = "/tmp/.litecfi_tmp";

  // Remove any existing temporary files
  std::string remove_temp_dir = "rm -rf " + temp_dir;
  system(remove_temp_dir.c_str());

  if (mkdir(temp_dir.c_str(), 0777) != 0) {
    return "";
  }

  std::ofstream libstack_stream(temp_dir + "/" + "__litecfi_stack_x86_64.S");
  if (FLAGS_shadow_stack == "avx_v3") {
    std::string libstack = Header() + CodegenStackInit(info) +
                           CodegenStack(info) + CodegenEmptyFunction() +
                           Footer();
    libstack_stream << libstack;
  } else {
    std::string libstack = Header() + CodegenStackInit(info) +
                           CodegenStackPush(info) + CodegenStackPop(info) +
                           CodegenEmptyFunction() + Footer();
    libstack_stream << libstack;
  }

  libstack_stream.close();

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
