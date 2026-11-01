// Linked into qwentts.cpp's qwen.dll (cmake/qwentts_win.cmake): vulkan-1.dll is delay-loaded; without it (no GPU driver)
// vkGetInstanceProcAddr gives only vkEnumerateInstanceVersion, which says Vulkan 1.0: its ggml (unlike llama.cpp's, which
// checks for a missing function) then finds Vulkan too old, reports no device, and qwen runs on the CPU.
#include <windows.h>
#include <delayimp.h>
#include <cstdint>
#include <cstring>

namespace {

int32_t __stdcall version_1_0(uint32_t *version) {
  *version = 1u << 22;  // VK_API_VERSION_1_0
  return 0;             // VK_SUCCESS
}

void *__stdcall proc(void *, const char *name) {
  return name && std::strcmp(name, "vkEnumerateInstanceVersion") == 0 ? reinterpret_cast<void *>(&version_1_0) : nullptr;
}

FARPROC WINAPI failed(unsigned reason, PDelayLoadInfo info) {
  if (reason == dliFailLoadLib) return reinterpret_cast<FARPROC>(GetModuleHandleW(nullptr));  // then each import fails
  if (reason == dliFailGetProc && info->dlp.fImportByName && std::strcmp(info->dlp.szProcName, "vkGetInstanceProcAddr") == 0)
    return reinterpret_cast<FARPROC>(&proc);
  return nullptr;
}

}  // namespace

extern "C" const PfnDliHook __pfnDliFailureHook2 = failed;
