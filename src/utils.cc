
#include "utils.h"

#include <limits.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

struct AnsiColor {
  static const std::string BLACK;
  static const std::string RED;
  static const std::string GREEN;
  static const std::string YELLOW;
  static const std::string BLUE;
  static const std::string MAGENTA;
  static const std::string CYAN;
  static const std::string WHITE;
  static const std::string END;

  static const std::string BOLD_BLACK;
  static const std::string BOLD_RED;
  static const std::string BOLD_GREEN;
  static const std::string BOLD_YELLOW;
  static const std::string BOLD_BLUE;
  static const std::string BOLD_MAGENTA;
  static const std::string BOLD_CYAN;
  static const std::string BOLD_WHITE;
};

const std::string AnsiColor::BLACK = "\033[30m";
const std::string AnsiColor::RED = "\033[31m";
const std::string AnsiColor::GREEN = "\033[32m";
const std::string AnsiColor::YELLOW = "\033[33m";
const std::string AnsiColor::BLUE = "\033[34m";
const std::string AnsiColor::MAGENTA = "\033[35m";
const std::string AnsiColor::CYAN = "\033[36m";
const std::string AnsiColor::WHITE = "\033[37m";
const std::string AnsiColor::END = "\033[0m";

const std::string AnsiColor::BOLD_BLACK = "\033[1;30m";
const std::string AnsiColor::BOLD_RED = "\033[1;31m";
const std::string AnsiColor::BOLD_GREEN = "\033[1;32m";
const std::string AnsiColor::BOLD_YELLOW = "\033[1;33m";
const std::string AnsiColor::BOLD_BLUE = "\033[1;34m";
const std::string AnsiColor::BOLD_MAGENTA = "\033[1;35m";
const std::string AnsiColor::BOLD_CYAN = "\033[1;36m";
const std::string AnsiColor::BOLD_WHITE = "\033[1;37m";

#define ColoredMessage(color, ansi_color) \
  case color:                             \
    return std::cout << AnsiColor::END << ansi_color;

#define UncoloredMessage() \
  case Color::NONE:        \
    return std::cout << AnsiColor::END;

std::ostream* dev_null = nullptr;

void DevNull(std::ostream** os) {
  if (!(*os)) {
    *os = new std::ofstream();
  }

  if (!reinterpret_cast<std::ofstream*>(*os)->is_open()) {
    reinterpret_cast<std::ofstream*>(*os)->open(
        "/dev/null", std::ofstream::out | std::ofstream::app);
  }
}

std::ostream& StdOut(Color color) {
  switch (color) {
    UncoloredMessage();
    ColoredMessage(Color::BLACK, AnsiColor::BLACK);
    ColoredMessage(Color::RED, AnsiColor::RED);
    ColoredMessage(Color::GREEN, AnsiColor::GREEN);
    ColoredMessage(Color::YELLOW, AnsiColor::YELLOW);
    ColoredMessage(Color::BLUE, AnsiColor::BLUE);
    ColoredMessage(Color::MAGENTA, AnsiColor::MAGENTA);
    ColoredMessage(Color::CYAN, AnsiColor::CYAN);
    ColoredMessage(Color::WHITE, AnsiColor::WHITE);

    ColoredMessage(Color::BOLD_BLACK, AnsiColor::BOLD_BLACK);
    ColoredMessage(Color::BOLD_RED, AnsiColor::BOLD_RED);
    ColoredMessage(Color::BOLD_GREEN, AnsiColor::BOLD_GREEN);
    ColoredMessage(Color::BOLD_YELLOW, AnsiColor::BOLD_YELLOW);
    ColoredMessage(Color::BOLD_BLUE, AnsiColor::BOLD_BLUE);
    ColoredMessage(Color::BOLD_MAGENTA, AnsiColor::BOLD_MAGENTA);
    ColoredMessage(Color::BOLD_CYAN, AnsiColor::BOLD_CYAN);
    ColoredMessage(Color::BOLD_WHITE, AnsiColor::BOLD_WHITE);
  }
  return std::cout << AnsiColor::END;
}

std::ostream& StdOut(Color color, bool logged) {
  if (logged) {
    return StdOut(color);
  }

  DevNull(&dev_null);
  return *dev_null;
}

std::ostream& Endl(std::ostream& os) {
  return os << AnsiColor::END << std::endl;
}

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

std::string GetFileNameFromPath(const std::string& s) {
  // Assume unix paths
  std::vector<std::string> elements = Split(s, '/');
  return elements[elements.size() - 1];
}

std::string GetCurrentDir() {
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  return std::string(result, (count > 0) ? count : 0);
}

std::string GetHomeDir() {
  struct passwd* pw = getpwuid(getuid());
  return std::string(pw->pw_dir);
}
