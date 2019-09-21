
#include <iostream>

int plt_call() {
  printf("Returning the answer to the Ultimate Question of Life, the Universe, "
         "and Everything!!\n");
  return 42;
}

int main() {
  std::cout << plt_call();
  return 0;
}
