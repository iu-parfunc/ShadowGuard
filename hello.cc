
int bar() { return 42; }

void baz(int depth) {
  if (depth == 0) {
    return;
  }

  // Use up some avx2 registers
  asm("pxor %%xmm8, %%xmm8;\n\t"
      "pxor %%xmm9, %%xmm9;\n\t"
      "pxor %%xmm10, %%xmm10;\n\t"
      :
      :
      :);
  baz(--depth);
  return;
}

int foo() { baz(1); }

int main() {
  long iters = 1;
  for (long i = 0; i < iters; i++) {
    baz(30);
  }
}
