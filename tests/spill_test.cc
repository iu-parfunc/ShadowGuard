
// Test Name : spill_test
//
// Description :
//
// Tests hardening an application causing a conflicting AVX2 register spill

void baz() {
  // Use up some avx2 registers
  asm("pxor %%xmm8, %%xmm8;\n\t"
      "pxor %%xmm9, %%xmm9;\n\t"
      "pxor %%xmm10, %%xmm10;\n\t"
      :
      :
      :);
}

int main() { baz(); }
