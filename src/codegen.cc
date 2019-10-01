
#include "codegen.h"

#include <stdlib.h>
#include <sys/stat.h>

#include <fstream>

#include "jit.h"
#include "utils.h"

DECLARE_string(shadow_stack);
DECLARE_bool(validate_frame);

std::string MemRegionInit() {
  std::string includes = "";
  includes += "#include <asm/prctl.h>\n";
  includes += "#include <stdint.h>\n";
  includes += "#include <stdlib.h>\n";
  includes += "#include <sys/prctl.h>\n";
  includes += "#include <sys/syscall.h>\n";
  includes += "#include <sys/types.h>\n";
  includes += "#include <unistd.h>\n";

  // Sets up the thread shadow stack.
  //
  // Format of the stack is
  //
  //           |   .   |
  //           |   .   |
  //           ---------
  //           |  RA1  | First stack entry
  //           ---------
  //           |  0x0  | Guard Words [8 or 16 bytes](To catch underflows)
  //           ---------
  // gs:0x0 -> |  SP   | Stack Pointer
  //           ---------

  std::string mem_init_fn = "";
  mem_init_fn += "void litecfi_init_mem_region() {\n";
  mem_init_fn += "  unsigned long addr = (unsigned long)malloc(1024);\n";
  mem_init_fn += "  if (syscall(SYS_arch_prctl, ARCH_SET_GS, addr) < 0)\n";
  mem_init_fn += "    abort();\n";
  mem_init_fn += "  addr += 8;\n";
  mem_init_fn += "  *((unsigned long*)addr) = 0;\n";
  mem_init_fn += "  addr += 8;\n";

  if (FLAGS_validate_frame) {
    mem_init_fn += "  *((unsigned long*)addr) = 0;\n";
    mem_init_fn += "  addr += 8;\n";
  }

  mem_init_fn += "  asm volatile(\"mov %0, %%gs:0;\" : : \"a\"(addr) :);\n";
  mem_init_fn += "}\n";

  return includes + mem_init_fn;
}

std::string Codegen() {
  std::string temp_dir = "/tmp/.litecfi_tmp";

  // Remove any existing temporary files
  std::string remove_temp_dir = "rm -rf " + temp_dir;
  system(remove_temp_dir.c_str());

  if (mkdir(temp_dir.c_str(), 0777) != 0) {
    return "";
  }

  std::string soname = "libstack.so";

  std::ofstream mem_init_c(temp_dir + "/" + "__litecfi_mem_init.c");
  mem_init_c << MemRegionInit();
  mem_init_c.close();

  std::string compile_so =
      "gcc -fpic -shared -o " + soname + " " + temp_dir + "/" + "*.c";

  if (system(compile_so.c_str()) != 0) {
    return "";
  }

  /*
  std::string remove_temp_dir = "rm -r " + temp_dir;
  system(remove_temp_dir.c_str());
  */

  return soname;
}
