
#include <iostream>

int leaf_fn(int* x) { return *x + 42; }

int non_leaf_fn(int* x) { return leaf_fn(x); }

int safe_non_leaf_fn() {
  int x = 34;
  return non_leaf_fn(&x);
}

int main() {
  std::cout << safe_non_leaf_fn();
  return 0;
}
