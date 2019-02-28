
// Test Name : spill_and_overflow_test
//
// Description :
//
// Tests hardening an application causing a register stack overflow along with a
// conflicting AVX2 register spill

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

int main() { baz(30); }
