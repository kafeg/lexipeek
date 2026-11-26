// Read aloud's system voice (spec M11): SAPI 5 says the text in the language it is in; Windows plays it itself, so the
// shell only polls whether it is still speaking.
#include "lexipeek/os.hpp"
#include "win.hpp"
// clang-format off
#include <objbase.h>
#include <sapi.h>
// clang-format on
#include <mutex>
#include <string>

namespace lexipeek::os {

namespace {
using namespace win;

std::mutex mu;
ISpVoice *voice = nullptr;  // made once, in the apartment of the UI thread that first speaks

// A voice's languages: SAPI keeps them in its Attributes as hex LCIDs, "407" or "407;9" for several.
bool token_speaks(ISpObjectToken *t, LANGID want) {
  ISpDataKey *key = nullptr;
  if (FAILED(t->OpenKey(L"Attributes", &key)) || !key) return false;
  LPWSTR langs = nullptr;
  bool ok = false;
  if (SUCCEEDED(key->GetStringValue(L"Language", &langs)) && langs) {
    for (std::wstring all(langs); !all.empty();) {
      auto at = all.find(L';');
      auto one = all.substr(0, at);
      all = at == std::wstring::npos ? L"" : all.substr(at + 1);
      if (PRIMARYLANGID(static_cast<LANGID>(wcstol(one.c_str(), nullptr, 16))) == want) ok = true;
    }
    CoTaskMemFree(langs);
  }
  key->Release();
  return ok;
}

// The first installed voice for lang ("de"): one whose languages hold its primary language, so de-AT reads German.
ISpObjectToken *voice_for(const std::string &lang) {
  auto id = LocaleNameToLCID(wide(lang).c_str(), 0);
  if (!id) return nullptr;
  auto want = static_cast<LANGID>(PRIMARYLANGID(LANGIDFROMLCID(id)));  // PRIMARYLANGID's macro gives an int
  ISpObjectTokenCategory *cat = nullptr;
  if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cat))) || !cat) return nullptr;
  ISpObjectToken *found = nullptr;
  IEnumSpObjectTokens *all = nullptr;
  if (SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE)) && SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &all)) && all) {
    for (ISpObjectToken *t = nullptr; !found && all->Next(1, &t, nullptr) == S_OK && t; t = nullptr) {
      if (token_speaks(t, want)) found = t;
      else t->Release();
    }
    all->Release();
  }
  cat->Release();
  return found;
}

}  // namespace

bool speak(const std::string &text, const std::string &lang) {
  std::lock_guard g(mu);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // the UI thread's apartment; already made: it says so and stays
  auto *token = voice_for(lang);
  if (!token) return false;
  if (!voice && FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&voice)))) {
    token->Release();
    return false;
  }
  voice->SetVoice(token);
  token->Release();
  // Async, dropping whatever it was saying: Windows plays it, the shell polls speaking().
  return SUCCEEDED(voice->Speak(wide(text).c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr));
}

bool speaking() {
  std::lock_guard g(mu);
  if (!voice) return false;
  SPVOICESTATUS st{};
  // Not SPRS_IS_SPEAKING: a text just queued is in neither state for a moment, and only SPRS_DONE means it is over.
  return SUCCEEDED(voice->GetStatus(&st, nullptr)) && st.dwRunningState != SPRS_DONE;
}

void stop_speaking() {
  std::lock_guard g(mu);
  if (voice) voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
}

}  // namespace lexipeek::os
