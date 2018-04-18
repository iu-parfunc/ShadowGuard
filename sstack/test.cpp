
#include <asmjit/asmjit.h>
#include <exception>
#include <string>
#include <stdio.h>

using namespace asmjit;

// Error handler that just prints the error and lets AsmJit ignore it.
class PrintErrorHandler : public asmjit::ErrorHandler {
  public:
    // Return `true` to set last error to `err`, return `false` to do nothing.
    bool handleError(asmjit::Error err, const char* message, asmjit::CodeEmitter* origin) override {
      fprintf(stderr, "ERROR: %s\n", message);
      return false;
    }
};

// Signature of the generated function.
typedef int (*Func)(void);

int main(int argc, char* argv[]) {
  JitRuntime rt;                          // Create a runtime specialized for JIT.
  PrintErrorHandler eh;

  CodeHolder code;                        // Create a CodeHolder.
  code.init(rt.getCodeInfo());            // Initialize it to be compatible with `rt`.
  code.setErrorHandler(&eh);

  X86Assembler a(&code);                  // Create and attach X86Assembler to `code`.

  // Let's get these registers from X86Assembler.
  X86Gp zbp = a.zbp();
  X86Gp zsp = a.zsp();

  int stackSize = 32;

  int* p = (int*)malloc(sizeof(int));

  // Function prolog.
  a.push(zbp);
  a.mov(zbp, zsp);
  a.sub(zsp, stackSize);

  // Encoding mov mem64, imm64
  a.mov(x86::rdi, (int64_t)p);
  a.mov(x86::qword_ptr(x86::rdi), imm(123));

  // Encoding mov mem32, imm32
  // However p >= 0xFFFFFFFFU would error
  a.mov(a.intptr_ptr((uint64_t)p), imm(153));

  // ... emit some code (this just sets return value to zero) ...
  a.xor_(x86::eax, x86::eax);

  // Function epilog and return.
  a.mov(zsp, zbp);
  a.pop(zbp);
  a.ret();

  // To make the example complete let's call it.
  Func fn;
  Error err = rt.add(&fn, &code);         // Add the generated code to the runtime.
  if (err) return 1;                      // Handle a possible error returned by AsmJit.

  int result = fn();                      // Execute the generated code.
  // printf("%d\n", result);                 // Print the resulting "0".

  printf("%d\n", *p);

  rt.release(fn);                         // Remove the function from the runtime.
  return 0;
}
