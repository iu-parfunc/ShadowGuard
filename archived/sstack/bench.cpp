
#include <asmjit/asmjit.h>
#include <exception>
#include <string>
#include <stdio.h>

#include "cycle.h"

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

  uint64_t* p = (uint64_t*)malloc(4*sizeof(uint64_t));
  p[0]        = (uint64_t) (p + 2);
  p[1]        = 0;
  p[2]        = (uint64_t) &&after;
  p[3]        = 0;

  asm("mov %0, %%r10\n\t" 
       :
       : "r" (p));

  Label nmatch = a.newLabel();
  Label abort  = a.newLabel();

  a.mov(x86::rdx, x86::ptr(x86::rsp));
  // Function prolog.
  a.push(zbp);
  a.mov(zbp, zsp);
  a.sub(zsp, stackSize);
  // a.mov(x86::rsi, (uint64_t)p);
  // a.mov(x86::fs, x86::rsi);

  a.mov(zsp, zbp);
  a.pop(zbp);

  // Encoding mov mem64, imm64
  a.mov(x86::rcx, (uint64_t) (p + 2));
  a.bind(nmatch);
  a.cmp(x86::qword_ptr(x86::rcx), imm(0));
  a.jz(abort);
  a.sub(x86::rcx, 8);
  a.cmp(x86::rdx, x86::ptr(x86::rcx, 8));
  a.jnz(nmatch);

  // a.add(x86::rcx, 8);
  // a.mov(x86::qword_ptr((uint64_t) (p+2)), x86::rcx);

  // Function epilog and return.
  a.ret();

  a.bind(abort);
  a.hlt();

  // To make the example complete let's call it.
  Func fn;
  Error err = rt.add(&fn, &code);         // Add the generated code to the runtime.
  if (err) return 1;                      // Handle a possible error returned by AsmJit.

  // CodeHolder code_1;                        // Create a CodeHolder.
  // code_1.init(rt.getCodeInfo());            // Initialize it to be compatible with `rt`.
  // code_1.setErrorHandler(&eh);
 
  code.reset();

  code.init(rt.getCodeInfo());

  X86Assembler a1(&code);                  // Create and attach X86Assembler to `code`.

  // Function prolog.
  a1.push(zbp);
  a1.mov(zbp, zsp);
  a1.sub(zsp, stackSize);

  // Function epilog and return.
  a1.mov(zsp, zbp);
  a1.pop(zbp);
  a1.ret();

  // To make the example complete let's call it.
  Func regular_fn;
  err = rt.add(&regular_fn, &code);         // Add the generated code to the runtime.
  if (err) return 1;                      // Handle a possible error returned by AsmJit.

  ticks scycles= 0;
  ticks start, end = 0;
  start = getticks();
  for (int i=0; i < 100000000; i++) {
    fn();                      // Execute the generated code.
after:
    scycles++;
  }

  end = getticks();
  scycles = end - start;

  ticks rcycles = 0;
  start = getticks();
  for (int i=0; i < 100000000; i++) {
    regular_fn();              // Execute the regular function
  }
  end   = getticks();
  rcycles = end - start;
  // printf("%d\n", result);                 // Print the resulting "0".

  rt.release(fn);                         // Remove the function from the runtime.

  printf("Regular function call cycles : %llu\n", rcycles);
  printf("Shadow stack call function call cycles : %llu\n", scycles);
  printf("Difference : %llu\n", scycles - rcycles);
  return 0;
}
