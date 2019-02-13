#ifndef LITECFI_CACHE_H_
#define LITECFI_CACHE_H_

#include <map>
#include <set>
#include <string>

struct SharedLibrary {
  // Fully qualified path to the shared library
  std::string path;
  // Register usage at each function in the shared library
  std::map<std::string, std::set<std::string>> register_usage;
};

std::map<std::string, SharedLibrary*>* GetRegisterAuditCache();

void FlushRegisterAuditCache(
    const std::map<std::string, SharedLibrary*>* const cache);

#endif  // LITECFI_CACHE_H_
