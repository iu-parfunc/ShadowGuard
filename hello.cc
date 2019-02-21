
#include <stdio.h>

#include "cycle.h"

int bar() { return 42; }

int baz() {
  // Use up some avx2 registers
  asm("pxor %%xmm8, %%xmm8;\n\t"
      "pxor %%xmm9, %%xmm9;\n\t"
      "pxor %%xmm10, %%xmm10;\n\t"
      :
      :
      :);
  return bar();
}

int foo() { return baz(); }

int main() {
  // long iters = 10000000;
  long iters = 1;
  ticks start = getticks();
  for (long i = 0; i < iters; i++) {
    baz();
  }
  ticks end = getticks();

  /*
  printf("elapsed : %llu\n", (end - start));
  printf("avg(cycles) : %llu\n", (end - start) / iters);
  */
}
