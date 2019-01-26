
#ifndef LITECFI_UTILS_H_
#define LITECFI_UTILS_H_

#include <iostream>
#include <string>
#include <vector>

std::vector<std::string> Split(const std::string& s, char delimiter);

template <typename T, typename U>
inline void PrintSequence(const T& collection, std::string delimiter = "") {
  std::cout << " [";

  long size = collection.size();
  long element_index = 0;
  for (const U& element : collection) {
    if (element_index == size - 1) {
      std::cout << element;
      break;
    }
    std::cout << element << delimiter;
    element_index++;
  }

  std::cout << "]" << std::endl;
}
#endif  // LITECFI_UTILS_H_
