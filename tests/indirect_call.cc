
#include <iostream>

int foo() { return 42; }

int indirect_call(int (*fp)()) { return fp(); }

int main() {
  std::cout << indirect_call(&foo);
  return 0;
}
