
#include <stdio.h>

void bar(int val) { printf("%d\n", val); }

void foo() {
  void (*fp)(int) = bar;
  fp(42);
}

int main() { foo(); }
