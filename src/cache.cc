
#include "cache.h"

#include <errno.h>
#include <sys/file.h>

#include <cstddef>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include "glog/logging.h"
#include "utils.h"

#if defined(__GLIBCXX__) || defined(__GLIBCPP__)
#include <ext/stdio_filebuf.h>
#else
#error We require libstdc++ at the moment. Compile with GCC or specify\
 libstdc++ at compile time (e.g: -stdlib=libstdc++ in Clang).
#endif

DECLARE_bool(vv);

const std::string kAuditCacheFile = ".audit_cache";

std::map<std::string, Code*>* GetRegisterAnalysisCache() {
  std::ifstream cache_file(kAuditCacheFile);

  std::map<std::string, Code*>* cache = new std::map<std::string, Code*>();

  if (cache_file.fail()) {
    return cache;
  }

  std::string line;
  while (std::getline(cache_file, line)) {
    std::vector<std::string> tokens = Split(line, ',');
    DCHECK(tokens.size() == 3) << line;

    std::vector<std::string> library_n_function = Split(tokens[0], '%');

    if (library_n_function.size() < 2) continue;

    std::string library = library_n_function[0];
    std::string function = library_n_function[1];

    Code* lib = nullptr;
    auto it = cache->find(library);
    if (it != cache->end()) {
      lib = it->second;
    } else {
      lib = new Code();
      lib->path = library;

      cache->insert(std::pair<std::string, Code*>(library, lib));

      if (FLAGS_vv) {
        StdOut(Color::GREEN)
            << "  >> Loading cached analysis results : " << library << Endl;
      }
    }

    std::vector<std::string> registers = Split(tokens[1], ':');
    DCHECK(registers.size() > 0);
    std::set<std::string> register_set(registers.begin(), registers.end());

    RegisterUsageInfo* info = new RegisterUsageInfo();
    info->used_ = register_set;

    if (tokens[2] == "1")
      info->writesMemory_ = true;
    else
      info->writesMemory_ = false;

    lib->register_usage[function] = info;
  }

  return cache;
}

void FlushRegisterAnalysisCache(
    const std::map<std::string, Code*>* const cache) {
  std::ofstream cache_file(kAuditCacheFile);

  if (!cache_file.is_open()) {
    char error[2048];
    fprintf(stderr,
            "Failed to create/ open the register audit cache file with : %s\n",
            strerror_r(errno, error, 2048));
    return;
  }

  // Exclusively lock the file for writing
  int fd =
      static_cast<__gnu_cxx::stdio_filebuf<char>* const>(cache_file.rdbuf())
          ->fd();

  if (flock(fd, LOCK_EX)) {
    char error[2048];
    fprintf(stderr, "Failed to lock the register audit cache file with : %s\n",
            strerror_r(errno, error, 2048));
    return;
  }

  for (auto const& it : *cache) {
    Code* lib = it.second;

    for (auto const& reg_iter : lib->register_usage) {
      std::set<std::string> registers = reg_iter.second->used_;

      if (registers.size() == 0) {
        continue;
      }

      std::string registers_concat = "";
      for (auto const& reg : registers) {
        registers_concat += (reg + ":");
      }

      // Remove the trailing ':'
      registers_concat.pop_back();
      cache_file << lib->path << "%" << reg_iter.first << ","
                 << registers_concat << "," << reg_iter.second->writesMemory_ << std::endl;     
    }
  }

  if (flock(fd, LOCK_UN)) {
    char error[2048];
    fprintf(stderr,
            "Failed to unlock the register audit cache file with : %s\n",
            strerror_r(errno, error, 2048));
  }

  cache_file.close();
  return;
}
