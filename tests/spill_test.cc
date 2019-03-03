
// Test Name : spill_test
//
// Description :
//
// Tests hardening an application causing a conflicting AVX2 register spill

void A() {
  // Use up some avx2 registers
  asm("pxor %%xmm1, %%xmm2;\n\t"
      "pxor %%xmm3, %%xmm9;\n\t"
      "pxor %%xmm10, %%xmm10;\n\t"
      :
      :
      :);
}

void baz() {
  // Use up some avx2 registers
  asm("pxor %%xmm8, %%xmm8;\n\t"
      "pxor %%xmm9, %%xmm9;\n\t"
      "pxor %%xmm10, %%xmm10;\n\t"
      :
      :
      :);
  A();

  asm("pxor %%xmm8, %%xmm8;\n\t"
      "pxor %%xmm9, %%xmm9;\n\t"
      "pxor %%xmm10, %%xmm10;\n\t"
      :
      :
      :);

}

int main() { baz(); }
