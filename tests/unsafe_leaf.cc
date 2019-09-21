
#include <iostream>

int unsafe_leaf_fn(int* x) {
  *x = 23;
  return *x + 42;
}

int main() {
  int x = 53;
  std::cout << unsafe_leaf_fn(&x);
  return 0;
}
