
#include <iostream>

int leaf_fn() { return 42; }

int indirect_call(int (*fp)()) { return fp(); }

int main() {
  std::cout << indirect_call(&leaf_fn);
  return 0;
}
