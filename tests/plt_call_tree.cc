
#include <iostream>

int plt_call() {
  printf("Returning the answer to the Ultimate Question of Life, the Universe, "
         "and Everything!!\n");
  return 42;
}

int plt_call_tree() { return plt_call(); }

int main() {
  std::cout << plt_call_tree();
  return 0;
}
