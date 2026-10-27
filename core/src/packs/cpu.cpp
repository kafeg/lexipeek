// The CPU's features by cpuid (a compiler intrinsic, not an OS call).
#include "lexipeek/packs.hpp"
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace lexipeek {

std::vector<std::string> cpu_features() {
  std::vector<std::string> out;
#if defined(_M_X64) || defined(__x86_64__)
  unsigned r1[4] = {}, r7[4] = {};
#if defined(_MSC_VER)
  int a[4];
  __cpuidex(a, 1, 0);
  for (int i = 0; i < 4; i++) r1[i] = static_cast<unsigned>(a[i]);
  __cpuidex(a, 7, 0);
  for (int i = 0; i < 4; i++) r7[i] = static_cast<unsigned>(a[i]);
  unsigned long long xcr0 = (r1[2] >> 27 & 1) ? _xgetbv(0) : 0;
#else
  __cpuid_count(1, 0, r1[0], r1[1], r1[2], r1[3]);
  __cpuid_count(7, 0, r7[0], r7[1], r7[2], r7[3]);
  unsigned long long xcr0 = 0;
  if (r1[2] >> 27 & 1) {
    unsigned lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    xcr0 = (static_cast<unsigned long long>(hi) << 32) | lo;
  }
#endif
  bool avx = (r1[2] >> 28 & 1) && (xcr0 & 6) == 6;  // the OS saves the YMM registers
  if (avx && (r7[1] >> 5 & 1)) out.push_back("avx2");
  if (avx && (r1[2] >> 12 & 1)) out.push_back("fma");
#endif
  return out;
}

}  // namespace lexipeek
