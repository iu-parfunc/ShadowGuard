
// Test Name : overflow_test
//
// Description :
//
// Tests hardening an application causing a register stack overflow

void baz(int depth) {
  if (depth == 0) {
    return;
  }

  baz(--depth);
  return;
}

int main() { baz(30); }
