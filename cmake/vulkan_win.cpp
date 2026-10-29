// Linked into every Windows program with the core: vulkan-1.dll is delay-loaded; without it (no GPU driver) its one
// import, vkGetInstanceProcAddr, finds nothing, ggml's Vulkan backend reports no device and llama.cpp runs on the CPU.
#include <windows.h>
#include <delayimp.h>
#include <cstring>

namespace {

void *__stdcall nothing(void *, const char *) { return nullptr; }

FARPROC WINAPI failed(unsigned reason, PDelayLoadInfo info) {
  if (reason == dliFailLoadLib) return reinterpret_cast<FARPROC>(GetModuleHandleW(nullptr));  // then each import fails
  if (reason == dliFailGetProc && info->dlp.fImportByName && std::strcmp(info->dlp.szProcName, "vkGetInstanceProcAddr") == 0)
    return reinterpret_cast<FARPROC>(&nothing);
  return nullptr;
}

}  // namespace

extern "C" const PfnDliHook __pfnDliFailureHook2 = failed;
