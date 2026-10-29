// `--self-test`: the OS parts that need no window, checked in place.
#include "lexipeek/app.hpp"
#include "lexipeek/os.hpp"

namespace lexipeek::os {

int self_test(std::ostream &out) {
  int failed = 0;
  auto check = [&](bool ok, const std::string &what) {
    out << (ok ? "ok   " : "FAIL ") << what << "\n";
    failed += !ok;
  };
  const std::string provider = "self-test";
  auto said = [](bool ok) { return ok ? std::string() : " " + last_error(); };
  bool w = set_secret(provider, "k-123 ключ");
  check(w, "secret: written" + said(w));
  check(secret(provider) == "k-123 ключ", "secret: read back as written");
  bool r = set_secret(provider, "");
  check(r, "secret: removed" + said(r));
  check(secret(provider).empty(), "secret: gone");
  bool again = set_secret(provider, "");
  check(again, "secret: removing a missing one is fine" + said(again));
  bool was = login();
  set_login(!was);
  check(login() == !was, "login: switched");
  set_login(was);
  check(login() == was, "login: back as it was");
  check(ram_mb() > 512, "ram: " + std::to_string(ram_mb()) + " MB");
  Hotkey h{"Q", true, false, true, true};
  bool reg = register_hotkey(90, h, [] {});
  check(reg, "hotkey: Ctrl+Alt+Shift+Q registered" + said(reg));
  unregister_hotkey(90);
  check(register_hotkey(90, h, [] {}), "hotkey: registered again after unregistering");
  unregister_hotkey(90);
  check(app::hotkey_label(Hotkey{"D", true, false, false, false}) == "Alt+D", "hotkey: label Alt+D");
  check(!register_hotkey(91, Hotkey{"%", true, false, false, false}, [] {}), "hotkey: a key it cannot take refused");
  if (auto run = cli_runner()) {
    ai::CliCall echo{"findstr", {"ein"}, {{"LEXIPEEK_X", "1"}}, "das ist ein Haus\nnichts\n", ""};
    auto ran = run(echo);
    check(ran.code == 0 && ran.out.find("das ist ein Haus") != std::string::npos && ran.out.find("nichts") == std::string::npos,
          "cli: a program with the prompt on stdin (" + std::to_string(ran.code) + ")");
    check(run({"no-such-program-lexipeek", {}, {}, "", ""}).code == 127, "cli: a missing program is 127");
  }
  register_scheme();
  auto scheme = scheme_command();
  check(scheme.find("LexiPeek.exe") != std::string::npos, "lexipeek:// opens " + scheme);
  auto font = icon_font();
  int ink = 0;
  for (auto a : glyph_mask(font, u'', 32)) ink += a > 0;
  check(!font.empty() && ink > 20, "icons: " + font + ", the warning glyph's pixels " + std::to_string(ink));
  return failed;
}

}  // namespace lexipeek::os
