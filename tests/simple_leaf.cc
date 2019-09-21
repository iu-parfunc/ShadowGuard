
#include <iostream>

int leaf_fn(int* x) { return *x + 42; }

int main() {
  int y = 53;
  std::cout << leaf_fn(&y);
  return 0;
}
