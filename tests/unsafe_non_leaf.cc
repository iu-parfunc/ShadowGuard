
#include <iostream>

int ns_leaf_fn(int* x) {
  *x = 24;
  return *x + 42;
}

int non_leaf_fn(int* x) { return ns_leaf_fn(x); }

int unsafe_non_leaf_fn() {
  int x = 34;
  return non_leaf_fn(&x);
}

int main() {
  std::cout << unsafe_non_leaf_fn();
  return 0;
}
