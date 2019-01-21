
#include <stdio.h>

#include "asmjit/asmjit.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

// Error handler that just prints the error and lets AsmJit ignore it.
class PrintErrorHandler : public asmjit::ErrorHandler {
 public:
  // Return `true` to set last error to `err`, return `false` to do nothing.
  bool handleError(asmjit::Error err, const char* message,
                   asmjit::CodeEmitter* origin) override {
    fprintf(stderr, "ERROR: %s\n", message);
    return false;
  }
};

using namespace asmjit::x86;

#define INIT_ASSEMBLER()       \
  asmjit::JitRuntime rt;       \
  PrintErrorHandler eh;        \
                               \
  asmjit::CodeHolder code;     \
  code.init(rt.getCodeInfo()); \
  code.setErrorHandler(&eh);   \
  asmjit::X86Assembler a(&code);

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Code for push maximum jump table slot size
  int max_size = 0;

  // Push quadword element 0
  {
    INIT_ASSEMBLER()
    a.pinsrq(xmm15, rdi, asmjit::imm(0));
    a.movq(rdi, mm7);
    a.add(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("push(element0) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  // Push quadword element 1
  {
    INIT_ASSEMBLER()
    a.pinsrq(xmm15, rdi, asmjit::imm(1));
    a.movq(rdi, mm7);
    a.add(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("push(element1) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  // Push quadword element 2
  {
    INIT_ASSEMBLER()
    a.vmovq(xmm15, rdi);
    a.vinserti128(ymm15, ymm0, xmm15, asmjit::imm(1));
    a.vpblendd(ymm0, ymm0, ymm15, asmjit::imm(48));
    a.movq(rdi, mm7);
    a.add(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("push(element2) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  // Push quadword element 3
  {
    INIT_ASSEMBLER()
    a.vmovq(xmm15, rdi);
    a.vinserti128(ymm15, ymm0, xmm15, asmjit::imm(1));
    a.vpblendd(ymm0, ymm0, ymm15, asmjit::imm(48));
    a.movq(rdi, mm7);
    a.add(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("push(element3) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  printf("\nmax(push) : %d\n\n", max_size);

  max_size = 0;

  // Pop quadword element 0
  {
    INIT_ASSEMBLER()
    a.vpextrq(rdi, xmm15, asmjit::imm(0));
    a.movq(rdi, mm7);
    a.sub(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("pop(element0) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  // Pop quadword element 1
  {
    INIT_ASSEMBLER()
    a.vpextrq(rdi, xmm15, asmjit::imm(1));
    a.movq(rdi, mm7);
    a.sub(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("pop(element1) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  // Pop quadword element 2
  {
    INIT_ASSEMBLER()
    a.vextracti128(xmm15, ymm15, asmjit::imm(1));
    a.vpextrq(rdi, xmm15, asmjit::imm(0));
    a.movq(rdi, mm7);
    a.sub(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("pop(element2) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  // Pop quadword element 3
  {
    INIT_ASSEMBLER()
    a.vextracti128(xmm15, ymm15, asmjit::imm(1));
    a.vpextrq(rdi, xmm15, asmjit::imm(1));
    a.movq(rdi, mm7);
    a.sub(rdi, asmjit::imm(1));
    a.movq(mm7, rdi);
    a.ret();

    int size = code.getCodeSize();
    printf("pop(element3) : %d\n", size);
    max_size = (size > max_size) ? size : max_size;
  }

  printf("\nmax(pop) : %d\n\n", max_size);

  {
    INIT_ASSEMBLER();
    a.lea(rsi, ptr(rip, 7));

    int size = code.getCodeSize();
    printf("size(lea) : %d\n\n", size);
  }

  {
    INIT_ASSEMBLER();

    asmjit::X86Mem c = ptr(rsi, rdi);
    a.jmp(c);

    int size = code.getCodeSize();
    printf("size(jmp) : %d\n\n", size);
  }

  return 0;
}
