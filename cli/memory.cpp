// Process memory for the bench: the CLI may use OS headers, the core may not.
#include "memory.hpp"
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <fstream>
#include <string>
#include <sys/resource.h>
#endif

namespace lexipeek::cli {

Memory memory() {
  Memory m;
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS c{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c)) {
    m.now_mb = static_cast<double>(c.WorkingSetSize) / 1048576.0;
    m.peak_mb = static_cast<double>(c.PeakWorkingSetSize) / 1048576.0;
  }
#else
  rusage u{};
  getrusage(RUSAGE_SELF, &u);
#if defined(__APPLE__)
  m.peak_mb = static_cast<double>(u.ru_maxrss) / 1048576.0;  // bytes on macOS
  task_vm_info_data_t info{};
  mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS)
    m.now_mb = static_cast<double>(info.phys_footprint) / 1048576.0;
#else
  m.peak_mb = static_cast<double>(u.ru_maxrss) / 1024.0;  // KB on Linux
  std::ifstream f("/proc/self/status");
  for (std::string line; std::getline(f, line);)
    if (line.starts_with("VmRSS:")) m.now_mb = std::stod(line.substr(6)) / 1024.0;
#endif
#endif
  return m;
}

}  // namespace lexipeek::cli
