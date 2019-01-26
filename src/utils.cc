
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

template <typename T>
void Split(const std::string& s, char delimiter, T result) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    *(result++) = item;
  }
}

std::vector<std::string> Split(const std::string& s, char delimiter) {
  std::vector<std::string> elements;
  Split(s, delimiter, std::back_inserter(elements));
  return elements;
}
