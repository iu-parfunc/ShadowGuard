
#include <iostream>

int foo() { return 42; }

int indirect_call(int (*fp)()) { return fp(); }

int indirect_call_tree(int (*fp)()) { return indirect_call(fp); }

int main() {
  std::cout << indirect_call_tree(&foo);
  return 0;
}
