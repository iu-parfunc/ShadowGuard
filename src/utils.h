
#ifndef LITECFI_UTILS_H_
#define LITECFI_UTILS_H_

#include <iostream>
#include <string>
#include <vector>

std::vector<std::string> Split(const std::string& s, char delimiter);

std::string GetFileNameFromPath(const std::string& s);

enum class Color : uint8_t {
  NONE = 0x29,
  BLACK = 0x30,
  RED = 0x31,
  GREEN = 0x32,
  YELLOW = 0x33,
  BLUE = 0x34,
  MAGENTA = 0x35,
  CYAN = 0x36,
  WHITE = 0x37,

  BOLD_BLACK = 0x40,
  BOLD_RED = 0x41,
  BOLD_GREEN = 0x42,
  BOLD_YELLOW = 0x43,
  BOLD_BLUE = 0x44,
  BOLD_MAGENTA = 0x45,
  BOLD_CYAN = 0x46,
  BOLD_WHITE = 0x47,
};

std::ostream& StdOut(Color color = Color::NONE);

std::ostream& StdOut(Color color, bool logged);

std::ostream& Endl(std::ostream& os);

template <typename T, typename U>
inline void PrintSequence(const T& collection, int left_pad = 0,
                          std::string delimiter = "") {
  std::string left_pad_str = "";
  while (left_pad > 0) {
    left_pad_str += " ";
    left_pad--;
  }

  StdOut(Color::NONE) << left_pad_str << "[";

  long size = collection.size();
  long element_index = 0;
  for (const U& element : collection) {
    if (element_index == size - 1) {
      StdOut(Color::NONE) << element;
      break;
    }
    StdOut(Color::NONE) << element << delimiter;
    element_index++;
  }

  StdOut(Color::NONE) << "]" << Endl;
}
#endif  // LITECFI_UTILS_H_
